#!/usr/bin/env python3
"""Check the native review view and its executable, not the Qt application."""

import pathlib
import re
import subprocess
import sys
import unittest

GENERATED = sys.argv[1] if __name__ == "__main__" and len(sys.argv) == 3 else None
EXECUTABLE = sys.argv[2] if GENERATED else None


@unittest.skipUnless(GENERATED and EXECUTABLE, "CTest supplies the generated header and linked executable")
class NativeReviewRendererBoundary(unittest.TestCase):
    def test_generated_view_uses_lvui_and_iigui_widgets(self):
        generated = pathlib.Path(GENERATED).read_text(encoding="utf-8")
        self.assertTrue("<LVUI/NativeButton.h>" in generated, "missing LVUI-owned button")
        self.assertIn("<iiGUI/NativeImageView.h>", generated, "missing iiGUI-native image view")
        self.assertIn("ii::gui::NativeImage::create", generated, "missing native image construction")
        self.assertFalse("<LVRS/" in generated, "generated view includes LVRS")
        self.assertFalse("lvrs::" in generated, "generated view calls LVRS")

    @unittest.skipUnless(sys.platform == "darwin", "Mach-O dependency inspection")
    def test_linked_view_has_no_qt_or_lvrs_dependency(self):
        executable = EXECUTABLE
        dependencies = subprocess.run(["otool", "-L", executable], check=True,
                                      capture_output=True, text=True).stdout
        self.assertNotRegex(dependencies, r"(?i)(libLVRS|Qt(?:Core|Gui|Qml|Quick))")
        symbols = subprocess.run(["nm", "-g", executable], check=True,
                                 capture_output=True, text=True).stdout
        self.assertIsNone(re.search(r"\b_+ZN4lvrs", symbols),
                          "the executable contains statically linked LVRS symbols")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: test_native_review_renderer_boundary.py GENERATED_HEADER BINARY")
    unittest.main(argv=[sys.argv[0]])
