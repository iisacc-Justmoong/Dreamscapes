#!/usr/bin/env python3
"""Check the native review view and its executable, not the Qt application."""

import pathlib
import re
import subprocess
import sys
import unittest

GENERATED = sys.argv[1] if __name__ == "__main__" and len(sys.argv) in (3, 4) else None
EXECUTABLE = sys.argv[2] if GENERATED else None
MODE = sys.argv[3] if GENERATED and len(sys.argv) == 4 else "image-button"


@unittest.skipUnless(GENERATED and EXECUTABLE, "CTest supplies the generated header and linked executable")
class NativeReviewRendererBoundary(unittest.TestCase):
    def test_generated_view_uses_lvui_and_iigui_widgets(self):
        generated = pathlib.Path(GENERATED).read_text(encoding="utf-8")
        if MODE == "menu":
            self.assertIn("<LVUI/NativeMenuItem.h>", generated, "missing LVUI-owned menu item")
            self.assertIn("<iiGUI/NativeMenuItemView.h>", generated, "missing iiGUI-native menu item")
        elif MODE == "grid":
            self.assertIn("<iiGUI/NativeImageView.h>", generated, "missing native result image")
            self.assertIn("<LVUI/NativeMenuItem.h>", generated, "missing LVUI context menu")
            self.assertIn("CollectionWidgetNavigation", generated, "missing native virtual collection navigation")
        elif MODE == "native-view":
            self.assertTrue("<LVUI/NativeButton.h>" in generated, "missing LVUI-owned button")
            self.assertTrue("<iiGUI/NativeView.h>" in generated and "ii::gui::NativeView::create" in generated,
                            "missing iiGUI-native composed view")
        elif MODE == "text-editor":
            self.assertTrue("<LVUI/NativeTextEditor.h>" in generated and
                            "<iiGUI/NativeTextEditorView.h>" in generated and
                            "NativeTextEditor::create" in generated, "missing LVUI and iiGUI text editor")
        elif MODE == "seed":
            for header in ("<LVUI/NativeInputField.h>", "<iiGUI/NativeInputFieldView.h>",
                           "<LVUI/NativePopover.h>", "<iiGUI/NativePopoverView.h>"):
                self.assertIn(header, generated, "missing native seed component")
        elif MODE == "button":
            self.assertIn("<LVUI/NativeButton.h>", generated, "missing LVUI button")
            self.assertIn("<iiGUI/NativeButtonView.h>", generated, "missing iiGUI button")
        elif MODE == "negative":
            self.assertIn("<LVUI/NativePopover.h>", generated, "missing LVUI popover")
            self.assertIn("<iiGUI/NativeView.h>", generated, "missing native prompt composition")
        elif MODE == "composed":
            self.assertIn("<iiGUI/NativeView.h>", generated, "missing iiGUI-native composed controls")
        else:
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
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] not in ("menu", "grid", "native-view", "text-editor", "seed", "button", "negative", "composed")):
        raise SystemExit("Usage: test_native_review_renderer_boundary.py GENERATED_HEADER BINARY [menu|grid|native-view|text-editor|seed|button|negative|composed]")
    unittest.main(argv=[sys.argv[0]])
