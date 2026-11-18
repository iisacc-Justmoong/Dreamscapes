"""Prevent retired Qt application lifecycle code from returning to the product."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/App"


class NativeLifecycleBoundary(unittest.TestCase):
    def test_retired_qt_entry_points_are_absent(self):
        for relative in ("src/main.cpp", "src/App/ApplicationLifetime.h", "tests/tst_applicationlifetime.cpp"):
            with self.subTest(path=relative):
                self.assertFalse((ROOT / relative).exists(), relative)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertNotIn("lvrs_add_qml_app(", cmake)
        self.assertNotIn("DreamscapesApplicationLifetimeTests", cmake)

    def test_native_lifecycle_objects_do_not_depend_on_qt(self):
        paths = [APP / "ApplicationShutdownCoordinator.h", APP / "ApplicationShutdownCoordinator.cpp"]
        paths += [APP / "Native" / f"{name}.{suffix}"
                  for name in ("NativeApplicationLifecycle", "NativeApplicationRuntime", "NativeApplicationWindow")
                  for suffix in ("h", "cpp")]
        paths.append(APP / "Native/NativeMain.cpp")
        qt = re.compile(r"#\s*include\s*[<\"](?:Qt|Q[A-Z])|\b(?:QObject|QCoreApplication|QGuiApplication|QEvent|qApp)\b")
        for path in paths:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertIsNone(qt.search(path.read_text()), path)

    def test_production_code_does_not_hook_qt_lifecycle(self):
        hooks = re.compile(r"QCoreApplication::aboutToQuit|QGuiApplication::applicationStateChanged|"
                           r"QEvent::Quit|setQuitOnLastWindowClosed|runBootstrappedQmlApp|configureApplicationLifetime")
        for path in APP.rglob("*"):
            if path.suffix not in {".cpp", ".h", ".mm"} or path.name.startswith("tst_"):
                continue
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertIsNone(hooks.search(path.read_text()), path)

    def test_executable_entry_points_do_not_create_qt_applications(self):
        qt_application = re.compile(r"\bQ(?:Core|Gui)?Application\b|runBootstrappedQmlApp")
        for path in APP.rglob("*.cpp"):
            if path.name.startswith("tst_"):
                continue
            text = path.read_text()
            if re.search(r"\bint\s+main\s*\(", text):
                with self.subTest(path=path.relative_to(ROOT)):
                    self.assertIsNone(qt_application.search(text), path)


if __name__ == "__main__":
    unittest.main()
