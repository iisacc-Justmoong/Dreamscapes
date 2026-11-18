"""Keep the migrated view and native-model boundaries free of QML files."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
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
        self.assertFalse(self.sources.qml, "QML views must not return to the application source")

    def test_video_playback_uses_the_native_clock(self):
        source = self.sources.native[APP / "ViewModels/Native/TimelinePlayback.h"]
        self.assertIn("std::chrono::steady_clock", source)
        self.assertNotIn("Date.now", source)

    def test_numeric_validation_is_not_javascript(self):
        source = self.sources.native[APP / "ViewModels/Native/NumberInputs.h"]
        self.assertIn("std::from_chars", source)
        self.assertIn("parseNumber", source)

    def test_production_has_no_javascript_state_modules(self):
        # This legacy icon-name table has no mutable view-model state and is not
        # part of the native application target.
        allowed = {APP / "Views/Editor/LvrsEditorIcons.js"}
        self.assertFalse(set(self.sources.js) - allowed)

    def test_domain_stores_are_native_forwards(self):
        for name in ("GenerationDraft.cpp", "StorageProjection.h", "VideoTimeline.cpp", "ViewCatalog.cpp"):
            self.assertIn(APP / "ViewModels/Native" / name, self.sources.native)

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
