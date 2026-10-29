"""Verify the complete Figma view inventory and its typed editing contracts."""
import collections
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EDITOR = ROOT / "src/App/Views/Editor"
DESIGN = ROOT.parents[1] / "Design/Dreamscapes/figma-adjust-filter-20261008"


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def rows(items):
    for row in items:
        yield row
        yield from rows(row.get("rows", []))


class GraphicPanelContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load(EDITOR / "GraphicPanels.json")
        cls.all = cls.catalog["panels"] + cls.catalog["overviews"]
        cls.by_id = {panel["id"]: panel for panel in cls.all}
        cls.by_name = {panel["name"]: panel for panel in cls.all}
        location = ROOT / "tools/generate_graphic_panels.py"
        spec = importlib.util.spec_from_file_location("graphic_generator", location)
        cls.generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.generator)

    def test_supplied_inventory_is_complete_and_unique(self):
        # This fixture is independently saved from the supplied 384-node list.
        fixture = load(EDITOR / "fixtures/GraphicPanelsFigma.json")
        expected = fixture.get("panels", fixture.get("frames", []))
        self.assertEqual(len(expected), 381)
        self.assertEqual({panel["id"] for panel in expected}, {panel["id"] for panel in self.catalog["panels"]})
        self.assertEqual(len(self.by_id), 384)
        self.assertEqual(len(self.by_name), 384)
        self.assertEqual(collections.Counter(panel["tool"] for panel in self.catalog["panels"]), {"color": 31, "adjust": 43, "mask": 18, "effects": 289})
        for expected_panel in expected:
            actual = self.by_id[expected_panel["id"]]
            for key in ("name", "width", "height"):
                self.assertEqual(actual[key], expected_panel[key], (actual["name"], key))

    def test_every_saved_autolayout_height_and_control_bounds(self):
        for panel in self.all:
            expected_height = 34 + 22 + sum(row["height"] for row in panel["rows"]) + 12 * len(panel["rows"])
            self.assertAlmostEqual(expected_height, panel["height"], msg=panel["name"])
            ids = [row["id"] for row in rows(panel["rows"])]
            self.assertEqual(len(ids), len(set(ids)), panel["name"])
            for row in rows(panel["rows"]):
                self.assertGreater(row["height"], 0, row["id"])
                self.assertLessEqual(row["width"], 398, row["id"])
                self.assertIn(row["type"], {"field", "section", "choices", "gallery", "action", "curve"})
                if row["type"] == "field" and row["control"] == "slider":
                    self.assertEqual(row["height"], 63)
                    self.assertTrue(all(math.isfinite(row[key]) for key in ("min", "max", "step")), row["id"])
                    self.assertGreater(row["step"], 0)
                    if row["initial"] is None:
                        self.assertTrue(row["optional"])
                    else:
                        self.assertTrue(math.isfinite(row["initial"]))
                        self.assertLessEqual(row["min"], row["initial"])
                        self.assertLessEqual(row["initial"], row["max"])

    def test_every_navigation_target_exists_in_the_correct_family(self):
        for panel in self.all:
            references = panel["navigation"] + panel.get("availableEditors", [])
            references += [value for row in rows(panel["rows"]) for value in [row] + row.get("options", [])]
            for reference in references:
                for field in ("target", "editorTarget"):
                    if reference.get(field):
                        target = reference[field]
                        self.assertIn(target, self.by_id, (panel["name"], field, target))
                        if self.by_id[target]["tool"] != panel["tool"]:
                            self.assertEqual({panel["tool"], self.by_id[target]["tool"]}, {"adjust", "mask"}, (panel["name"], target))
            if panel.get("parent"):
                self.assertIn(panel["parent"], self.by_id)

    def test_empty_and_processing_apply_preserve_original_disabled_kind(self):
        disabled = [panel for panel in self.catalog["panels"] if panel.get("status") in {"empty", "processing"}]
        self.assertEqual(len(disabled), 6)
        for panel in disabled:
            footer = next(row for row in panel["rows"] if row["id"].endswith("/footer"))
            apply = footer["options"][-1]
            self.assertEqual(apply["label"], "Applying…" if panel["status"] == "processing" else "Apply")
            self.assertFalse(apply["enabled"])
            self.assertTrue(all(option.get("enabled", True) for option in footer["options"][:-1]))
        if DESIGN.exists():
            originals = {}
            for readback in (DESIGN / "build").glob("readback-*.json"):
                data = load(readback)
                if isinstance(data, dict):
                    for panel in data.get("panels", []):
                        if panel.get("name") in {view["name"] for view in disabled} and panel.get("apply"):
                            originals[panel["name"]] = panel["apply"]
            self.assertEqual(set(originals), {panel["name"] for panel in disabled})
            for panel in disabled:
                original = originals[panel["name"]]
                self.assertEqual(original["kind"], "disabled")
                self.assertTrue(original["disabled"])
                self.assertEqual(panel["rows"][-1]["options"][-1]["id"], original["id"])

    def test_overviews_connect_every_editable_view_and_status_registry_covers_results(self):
        pending = list(self.catalog["defaults"].values()) + [panel["id"] for panel in self.catalog["overviews"]]
        reached = set(pending)
        while pending:
            panel = self.by_id[pending.pop()]
            references = panel["navigation"] + panel.get("availableEditors", [])
            references += [value for row in rows(panel["rows"]) for value in [row] + row.get("options", [])]
            for reference in references:
                for field in ("target", "editorTarget"):
                    target = reference.get(field)
                    if target in self.by_id and target not in reached:
                        reached.add(target)
                        pending.append(target)
        for panel in self.catalog["panels"]:
            if panel["presentation"] == "status":
                self.assertEqual(self.catalog["statusViews"][panel["tool"]][panel["status"]], panel["id"])
            else:
                self.assertIn(panel["id"], reached, panel["name"])

    def test_typed_ranges_enums_and_parameters_match_original_source(self):
        if not DESIGN.exists():
            self.skipTest("Authoring source is not available in this checkout")
        specs = {state["key"]: state for state in load(DESIGN / "expanded-states.json")["states"]}
        ledger = {panel["key"]: panel for panel in load(DESIGN / "state.json")["panels"]}
        for name, source in specs.items():
            panel = self.by_name[name]
            self.assertEqual(panel["operation"], source["api"])
            if "command" in source:
                self.assertEqual(panel["sourceCommand"], source["command"])
            expected = {record["id"]: record for record in ledger[name]["controls"]}
            actual = {row["id"]: row for row in rows(panel["rows"]) if row.get("parameter")}
            self.assertEqual(set(expected), set(actual), name)
            for identifier, record in expected.items():
                row = actual[identifier]
                self.assertEqual(row["parameter"], record["parameter"], name)
                self.assertEqual(row["label"], record["name"], name)
                if record["kind"] == "number":
                    self.assertEqual([row["min"], row["max"]], record["range"], (name, row["parameter"]))
                elif record["kind"] == "choice":
                    self.assertEqual([option["label"] for option in row["options"]], record["options"])
                    self.assertEqual(row["selectedIndex"], record["selected"])
        self.assertEqual(self.catalog["source"]["expandedStatesSha256"], hashlib.sha256((DESIGN / "expanded-states.json").read_bytes()).hexdigest())

    def test_collection_defaults_and_ui_selection_are_distinct(self):
        curve = self.by_name["Color_Curve"]
        curve_rows = list(rows(curve["rows"]))
        point_editor = next(row for row in curve_rows if row.get("parameter") == "master.points")
        self.assertEqual(point_editor["initial"], [{"input": 0, "output": 0}, {"input": 1, "output": 1}])
        selector = next(row for row in curve_rows if row["type"] == "choices")
        self.assertTrue(selector["uiOnly"])
        for panel in self.all:
            for row in rows(panel["rows"]):
                if row["type"] == "curve":
                    self.assertNotIn("parameter", row)
                if "[]" in row.get("parameter", ""):
                    self.assertIn("collection", row)
                    if row["collection"]["index"] is None:
                        self.assertFalse(row["collection"]["existingItem"])
                    else:
                        self.assertGreaterEqual(row["collection"]["index"], 0)
        color = self.by_name["Color_PointColor"]
        self.assertEqual(color["availableEditors"][0]["target"], self.by_name["Color_PointColor_EditPoints"]["id"])

    def test_sdk_binding_preserves_source_fields_and_required_host_input(self):
        for panel in self.catalog["panels"]:
            self.assertEqual(panel["operation"], panel["api"])
        for name in ("Effects_Shear", "Effects_Offset"):
            field = next(row for row in rows(self.by_name[name]["rows"]) if row.get("parameter") == "e")
            self.assertEqual(field["sdkParameter"], "edge")
            self.assertEqual(field["initial"], "Wrap")
        diagnostics = [panel for panel in self.catalog["panels"] if panel["api"] in {"maskColorOverlay", "maskColorOnBwOverlay", "maskImageOnBlack", "maskImageOnWhite", "maskWhiteOnBlack"}]
        self.assertEqual(len(diagnostics), 5)
        for panel in diagnostics:
            selector = next(row for row in rows(panel["rows"]) if row.get("parameter") == "previewMode")
            self.assertTrue(selector["uiOnly"])
            self.assertIn("no previewMode SDK field exists", selector["sourceFieldException"])
        samples = next(row for row in rows(self.by_name["Mask_Color"]["rows"]) if row.get("parameter") == "samples")
        self.assertEqual(samples["initial"], [])
        self.assertEqual((samples["minItems"], samples["maxItems"]), (1, 8))
        self.assertTrue(self.by_name["Mask_Color"]["requires"])
        picker = self.by_name["Effects_Displace_EditMap"]
        self.assertEqual(picker["editor"], "imageInput")
        self.assertEqual(picker["sourceParameter"], "map")
        preset = next(row for row in rows(self.by_name["Color_WB"]["rows"]) if row.get("parameter") == "preset")
        self.assertEqual(preset["options"][1]["api"], "whiteBalancePreset")
        self.assertTrue(preset["uiOnly"])

    def test_native_catalog_and_manifest_are_the_same_generated_catalog(self):
        native = ROOT / "resources/viewmodels/graphic.json"
        self.assertEqual(json.loads(native.read_text(encoding="utf-8")), self.catalog)
        self.assertNotIn("https://www.figma.com/api/mcp/asset", native.read_text())
        self.assertFalse((EDITOR / "EditorGraphicDefinitions.js").exists())

    def test_reviewed_manifest_can_regenerate_and_validate_native_data(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="native-catalog-") as directory:
            stage = Path(directory)
            command = [sys.executable, str(ROOT / "tools/generate_graphic_panels.py"), "--from-manifest", str(EDITOR / "GraphicPanels.json"), "--output", str(stage / "manifest.json"), "--native-catalog", str(stage / "native.json")]
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(load(stage / "native.json"), self.catalog)
            subprocess.run(command + ["--check"], check=True, capture_output=True)
            changed = load(stage / "native.json")
            changed["panels"].pop()
            (stage / "native.json").write_text(json.dumps(changed))
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)

    def test_high_fidelity_evidence_is_honestly_counted(self):
        available = sum(panel["evidence"]["highFidelity"] for panel in self.catalog["panels"])
        missing = set(self.catalog["evidence"]["unavailableContextIds"])
        self.assertEqual(available, self.catalog["evidence"]["highFidelityContexts"])
        self.assertEqual(available + len(missing), 381)
        self.assertEqual(missing, {panel["id"] for panel in self.catalog["panels"] if not panel["evidence"]["highFidelity"]})


if __name__ == "__main__":
    unittest.main()
