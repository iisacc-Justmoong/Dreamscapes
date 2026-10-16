"""Verify the independent ten-view Figma baseline and exported Elements assets."""
import hashlib
import json
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
EDITOR = ROOT / "src/App/Views/Editor"
DESIGN = ROOT / "build/elements-figma-20261009/figma"


class ElementsPanelContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((EDITOR / "fixtures/ElementsPanelsFigma.json").read_text())
        cls.views = {view["id"]: view for view in cls.fixture["views"]}

    def test_requested_frames_have_distinct_state_contracts(self):
        self.assertEqual(self.fixture["fileKey"], "bn8O4AHKr1X9DWnhR1TgEy")
        self.assertEqual({key: view["nodeId"] for key, view in self.views.items()}, {
            "rectangle": "585:94650", "ellipse": "650:15792", "polygon": "650:15793",
            "line": "650:15794", "fill": "650:15795", "stroke": "650:15796",
            "selection": "650:15797", "inspect": "662:16150",
            "multiple": "662:16151", "clear": "662:16152",
        })
        self.assertEqual(len(self.fixture["views"]), 10)
        expected_fields = {
            "rectangle": [0, 3, 1, 8], "ellipse": [0, 3, 4, 5],
            "polygon": [0, 3, 7, 9, 8], "line": [0, 3],
            "fill": [2, 16, 14, 15], "stroke": [10, 17, 6, 11, 12, 13],
            "selection": [], "inspect": [0, 1, 16, 10], "multiple": [], "clear": [],
        }
        for key, indexes in expected_fields.items():
            self.assertEqual([control["fieldId"] for control in self.views[key]["controls"]],
                             [f"field-{index}" for index in indexes], key)
        self.assertEqual(self.fixture["tabs"], ["Selection", "Shape", "Fill", "Stroke"])

    def test_saved_controls_remain_inside_the_reference_frame(self):
        geometry = self.fixture["geometry"]
        self.assertEqual(geometry["frame"], [432, 559])
        self.assertEqual([geometry["inset"], geometry["contentWidth"], geometry["gap"]], [17, 398, 12])
        for view in self.views.values():
            previous_bottom = 0
            for control in view["controls"]:
                x, y, width, height = control["rect"]
                self.assertEqual([x, width], [17, 398], (view["id"], control["fieldId"]))
                self.assertGreaterEqual(y, previous_bottom)
                self.assertLessEqual(y + height, 542)
                self.assertGreater(height, 0)
                previous_bottom = y + height
                if control["kind"] == "slider":
                    self.assertEqual(height, 52)
                elif control["kind"] == "color":
                    self.assertEqual(height, 53)
        for key in ["multiple", "clear"]:
            self.assertEqual(self.views[key]["selectionRows"]["names"], ["Rectangle", "Ellipse", "Polygon"])
        self.assertEqual(self.views["multiple"]["selectionRows"]["selected"], [True, False, False])
        self.assertEqual(self.views["clear"]["selectionRows"]["selected"], [False, False, False])

    def test_baseline_matches_saved_high_fidelity_figma_contexts(self):
        if not DESIGN.exists():
            self.skipTest("Figma authoring evidence is not available in this checkout")
        for view in self.views.values():
            stem = view["nodeId"].replace(":", "-")
            context = DESIGN / "contexts" / f"{stem}-0.txt"
            screenshot = DESIGN / "screenshots" / f"{stem}.png"
            self.assertEqual(hashlib.sha256(context.read_bytes()).hexdigest(), view["contextSha256"])
            self.assertEqual(hashlib.sha256(screenshot.read_bytes()).hexdigest(), view["screenshotSha256"])
            text = context.read_text().split("export default")[-1]
            self.assertIn(f'data-node-id="{view["nodeId"]}" data-name="{view["name"]}"', text)
            self.assertIn("h-[30px]", text)
            self.assertIn("w-[398px]", text)
            for control in view["controls"]:
                self.assertIn(control["label"], text, (view["id"], control["fieldId"]))
            for label in view.get("labels", []):
                self.assertIn(label, text, view["id"])

    def test_every_exported_svg_keeps_its_original_bytes_and_root_geometry(self):
        self.assertEqual(len(self.fixture["assets"]), 23)
        for asset in self.fixture["assets"]:
            path = EDITOR / "Assets/Elements" / asset["name"]
            self.assertTrue(path.is_file(), asset["name"])
            self.assertGreater(path.stat().st_size, 0, asset["name"])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), asset["sha256"], asset["name"])
            root = ET.parse(path).getroot()
            self.assertEqual(root.attrib.get("width"), asset["width"], asset["name"])
            self.assertEqual(root.attrib.get("height"), asset["height"], asset["name"])
            self.assertTrue(root.attrib.get("viewBox"), asset["name"])
        dimensions = {asset["name"]: [asset["width"], asset["height"]] for asset in self.fixture["assets"]}
        self.assertEqual(dimensions["auto-refresh.svg"], ["16", "16"])
        for name in ["rectangle", "ellipse", "polygon", "line", "fill", "stroke", "direct-select", "group", "unselect-all"]:
            self.assertEqual(dimensions[name + ".svg"], ["18", "18"])

    def test_elements_are_routed_to_the_dedicated_view_on_desktop_and_mobile(self):
        for name in ["EditorDesktopPanel.qml", "EditorToolSheet.qml"]:
            source = (EDITOR / name).read_text()
            self.assertIn("EditorElementsPanel", source, name)
            self.assertRegex(source, r'toolId\s*===\s*"elements"', name)
        source = (EDITOR / "EditorElementsPanel.qml").read_text()
        self.assertNotRegex(source, r"https?://(?:www\.)?figma\.com/api/mcp/asset/")
        self.assertIn('objectName: "editorElementsPanel"', source)
        self.assertIn("selectView", source)
        for name in ["elementsViewTab", "elementsShapeChoice-", "elementsSelectionRow-", "elementsSelectionCheck-"]:
            self.assertIn(name, source)
        # The real document is the inventory; a permanent three-row mock would
        # falsely show selectable shapes on a newly created empty canvas.
        self.assertIn("elementCandidates", source)

    def test_view_catalog_matches_the_independent_visible_field_lists(self):
        source = (EDITOR / "EditorElementsDefinitions.js").read_text()
        records = re.findall(r'\{id: "([^"]+)", nodeId: "([^"]+)"[^\n]+fields: (\[[^\]]*\])\}', source)
        self.assertEqual(len(records), 10)
        for identifier, node, field_list in records:
            self.assertIn(identifier, self.views)
            baseline = self.views[identifier]
            self.assertEqual(node, baseline["nodeId"], identifier)
            self.assertEqual(json.loads(field_list), [control["fieldId"] for control in baseline["controls"]], identifier)


if __name__ == "__main__":
    unittest.main()
