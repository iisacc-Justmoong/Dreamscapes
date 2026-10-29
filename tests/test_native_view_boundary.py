"""Prevent executable QML reducers from returning to the migrated boundaries."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/App"

class SourceSnapshot:
    """Read each checked source once; independent filesystem reads are bounded."""
    def __init__(self, root, paths=None, reader=None):
        paths = sorted(root.rglob("*") if paths is None else paths)
        native = root / "ViewModels/Native"
        qml = [path for path in paths if path.suffix == ".qml"]
        cpp = [path for path in paths if path.suffix == ".cpp" and not path.name.startswith("tst_")]
        native_paths = [path for path in paths if path.parent == native and path.suffix in {".cpp", ".h"}]
        selected = sorted(set(qml + cpp + native_paths))
        read = reader if reader is not None else lambda path: path.read_text(encoding="utf-8")
        with ThreadPoolExecutor(max_workers=8) as pool:
            sources = dict(zip(selected, pool.map(read, selected)))
        self.qml = {path: sources[path] for path in qml}
        self.cpp = {path: sources[path] for path in cpp}
        self.native = {path: sources[path] for path in native_paths}
        self.js = tuple(path for path in paths if path.suffix == ".js")


class NativeViewBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = SourceSnapshot(APP)

    def test_data_reducers_execute_in_native_models(self):
        # Only calculations over rendered UI-item geometry stay in QML.
        # These three expressions cannot inspect files, jobs or draft values.
        geometry = {
            "Views/Editor/EditorToolControl.qml": ("items: widths.map(",),
            "Views/Editor/EditorTimeline.qml": ("row.children.filter(", "items.reduce("),
        }
        reducer = re.compile(r"\.(?:map|filter|sort|reduce|findIndex)\s*\(|Object\.assign|Array\.prototype")
        for path, source in self.sources.qml.items():
            name = path.relative_to(APP).as_posix()
            for line in source.splitlines():
                if reducer.search(line):
                    self.assertTrue(any(allowed in line for allowed in geometry.get(name, ())),
                                    f"QML data reducer must be native: {name}: {line.strip()}")

    def test_video_playback_uses_the_native_clock(self):
        source = self.sources.qml[APP / "Views/Video/VideoTimeline.qml"]
        self.assertIn("NativeTimelinePlayback", source)
        self.assertNotIn("Date.now", source)
        self.assertNotRegex(source, r"\bTimer\s*\{")

    def test_numeric_validation_is_not_javascript(self):
        for path, source in self.sources.qml.items():
            self.assertNotRegex(source,r"\b(?:parseInt|parseFloat)\s*\(|Number\.NaN|\.match\s*\(",str(path))

    def test_production_has_no_javascript_state_modules(self):
        self.assertEqual(self.sources.js, ())
        for path, source in self.sources.qml.items():
            self.assertNotRegex(source, r'import[^\n]+["\'][^"\']+\.js["\']', str(path))

    def test_domain_stores_are_native_forwards(self):
        files = ["Main.qml", "Views/Editor/EditorToolSheet.qml", "Views/Editor/EditorGraphicPanel.qml",
                 "Views/Home/NewCanvasDialog.qml", "Views/Home/StorageBrowser.qml", "Views/Home/ImageSelectionBar.qml",
                 "Views/Home/QuickGenerate.qml", "Views/Home/DesktopGenerationComposer.qml", "Views/Video/VideoTimelineState.qml"]
        for name in files:
            source = self.sources.qml[APP / name]
            self.assertNotIn("Object.assign", source, name)
            self.assertNotIn("JSON.parse", source, name)
            self.assertNotIn("JSON.stringify", source, name)
        self.assertIn("NativeVideoTimeline", self.sources.qml[APP / files[-1]])
        for name in ["Views/Home/HomeRecentFiles.qml", "Views/Home/HomeQueueBar.qml", "Views/Home/HomeSelectedFile.qml"]:
            source = self.sources.qml[APP / name]
            for reducer in (".filter(", ".find(", ".some(", "Object.assign"):
                self.assertNotIn(reducer, source, name)

    def test_native_core_cannot_include_gui_or_product_backends(self):
        for path, source in self.sources.native.items():
            for line in source.splitlines():
                if not line.lstrip().startswith("#include"): continue
                for forbidden in ("<Q", "Qt", "EditorCanvas.h", "GenerationController.h", "WorkspaceSession.h"):
                    self.assertNotIn(forbidden, line, str(path))

    def test_legacy_execution_is_test_only(self):
        for path, source in self.sources.cpp.items():
            self.assertNotRegex(source, r'(?:QJSEngine|QQmlEngine)[^;]*\bevaluate\s*\(', str(path))


class SourceSnapshotContract(unittest.TestCase):
    def test_each_checked_source_is_read_once(self):
        qml, cpp = APP / "Fixture.qml", APP / "ViewModels/Native/Fixture.cpp"
        header = APP / "ViewModels/Native/Fixture.h"
        contents = {qml: "Item {}", cpp: "// native implementation", header: "#pragma once"}
        reads = []
        def read(path):
            reads.append(path)
            return contents[path]
        snapshot = SourceSnapshot(APP, [qml, cpp, header, APP / "tst_Fixture.cpp", APP / "ignored.png"], read)
        self.assertEqual(Counter(reads), Counter({path: 1 for path in contents}))
        self.assertEqual(snapshot.qml[qml], contents[qml])
        self.assertEqual(snapshot.cpp[cpp], snapshot.native[cpp])
        self.assertEqual(snapshot.native[header], contents[header])

    def test_snapshot_still_rejects_regressions(self):
        contents = {
            APP / "Unsafe.qml": "property var values: items.map(item => item)",
            APP / "Unsafe.cpp": 'auto value = QJSEngine().evaluate("source");',
            APP / "ViewModels/Native/Unsafe.h": "#include <QObject>",
        }
        boundary = NativeViewBoundary()
        boundary.sources = SourceSnapshot(APP, [*contents, APP / "unsafe.js"], contents.__getitem__)
        for check in (boundary.test_data_reducers_execute_in_native_models,
                      boundary.test_legacy_execution_is_test_only,
                      boundary.test_native_core_cannot_include_gui_or_product_backends,
                      boundary.test_production_has_no_javascript_state_modules):
            with self.subTest(check=check.__name__), self.assertRaises(AssertionError):
                check()

if __name__ == "__main__": unittest.main()
