#!/usr/bin/env python3
"""Generate editable Dreamscapes panel data from the saved Figma harvest.

Geometry, semantic parameter keys and prototype destinations come from saved
frames. High fidelity design contexts verify labels, control types and assets.
The generated JS deliberately contains no file I/O or QML imports.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import html
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILE_KEY = "bn8O4AHKr1X9DWnhR1TgEy"
COUNTS = {"color": 31, "adjust": 43, "mask": 18, "effects": 289}
DEFAULT_NAMES = {"color": "Color_Light", "adjust": "Adjust_Light", "mask": "Mask_Brush", "effects": "Effects_Category_Blur"}
SEMANTIC = ("Parameter", "Input", "Toggle", "Choice", "Section", "Actions", "Tools", "Tool")
LEGACY_COLOR = {"exposure": "field-1", "contrast": "field-4", "highlights": "field-8", "shadows": "field-12", "whites": "field-16", "blacks": "field-20", "temperature": "field-25", "tint": "field-28", "vibrance": "field-32", "saturation": "field-36"}
ICON_NAMES = {"tone": "Histogram", "adjust": "Adjustment", "filter": "Filter", "color": "Color", "curve": "ToneCurveChannel", "grade": "ColorGrading", "blur": "Blur", "detail": "Sharpen", "noise": "NoiseEffect", "lens": "LensCorrection", "mask": "LocalAdjustment", "picker": "ColorPicker", "palette": "ColorPalette"}


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def children(node):
    return node.get("children", [])


def walk(node):
    yield node
    for child in children(node):
        yield from walk(child)


def node_texts(node):
    """The compact harvest keeps each semantic container's ordered text list."""
    found = node.get("texts")
    if found is not None:
        return [str(t.get("characters", t.get("text", "")) if isinstance(t, dict) else t).strip() for t in found if t]
    result = []
    for child in walk(node):
        value = child.get("characters", child.get("text", ""))
        if value:
            result.append(str(value).strip())
    return result


def targets(node):
    result = []
    def visit(value):
        if isinstance(value, dict):
            if value.get("destinationId"):
                result.append(value["destinationId"])
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
    visit(node.get("reactions", []))
    return list(dict.fromkeys(result))


def semantic(node):
    name = node.get("name", "")
    return tuple(name.split(" · ", 1)) if " · " in name else (name, "")


def displayed_value(value):
    value = value.strip().replace("−", "-").replace("+", "")
    if value.lower() in {"true", "false", "on", "off", "enabled", "disabled"}:
        return value.lower() in {"true", "on", "enabled"}
    try:
        if value.startswith("[") or value.startswith("{"):
            return json.loads(value)
    except ValueError:
        pass
    match = re.fullmatch(r"(-?(?:\d+(?:\.\d*)?|\.\d+))(?:\s*(?:%|°|px|EV|K|mm))?", value)
    if match:
        number = float(match.group(1))
        return int(number) if number.is_integer() else number
    return value


def numeric_contract(text, initial):
    normalized = text.replace("−", "-")
    numbers = re.findall(r"-?(?:\d+(?:\.\d*)?|\.\d+)", normalized)
    if len(numbers) < 2 or not any(token in normalized for token in ("…", "..", "–", "—", " to ")):
        raise ValueError("Numeric parameter has no source range: " + text)
    minimum, maximum = map(float, numbers[:2])
    if minimum > maximum:
        raise ValueError("Inverted source numeric range: " + text)
    unit = next((unit for unit in ("%", "°", "px", "EV", "K", "mm") if unit in text), "")
    decimals = [len(n.split(".", 1)[1]) for n in numbers if "." in n]
    if isinstance(initial, float) and not initial.is_integer():
        decimals.append(len(str(initial).split(".", 1)[1]))
    step = 10 ** -max(decimals) if decimals else 1
    return {"min": minimum, "max": maximum, "step": step, "unit": unit, "rangeText": text}


def jsx_evidence(source):
    """Only successful high fidelity responses count as design evidence."""
    if "export default" not in source:
        return None
    root_source = source[source.index("export default"):]
    strings = [html.unescape(re.sub(r"\s+", " ", value).strip()) for value in re.findall(r">([^<>]+)<", root_source)]
    return {
        "nodes": set(re.findall(r'data-node-id="([^"]+)"', root_source)),
        "texts": set(value for value in strings if value and not value.startswith("{")),
        "assets": sorted(set(re.findall(r"const (img\w+) = .*?/([^/`]+)`,?;", source))),
        "sha256": hashlib.sha256(source.encode()).hexdigest(),
    }


def icon_name(node, context):
    for descendant in walk(node):
        candidate = descendant.get("icon", "")
        if candidate:
            return candidate
    # High fidelity JSX identifies reused component functions without paths.
    if context:
        identifier = re.escape(node.get("id", ""))
        match = re.search(r'data-node-id="' + identifier + r'"[^>]*>(.*?)</(?:a|div|button)>', context, re.S)
        if match:
            icons = re.findall(r"<([A-Z][A-Za-z0-9]+)\b", match.group(1))
            if icons:
                return icons[0]
    return ""


def make_row(node, panel, context, source_controls=None):
    kind, key = semantic(node)
    texts = node_texts(node)
    row = {"id": node["id"], "figmaNode": node["id"] if "/" not in node["id"] else None, "height": node.get("height", 22), "width": node.get("width", 398), "label": texts[0] if texts else key}
    if "/note/" in node["id"]:
        row["style"] = "caption"
    source = (source_controls or {}).get(node["id"])
    if source:
        row.update(label=source["label"], parameter=source["parameter"], api=source.get("api", panel["api"]))
        for attribute in ("sdkUnit", "enumType", "enumName", "sourceArgument", "sourceFieldException", "sourceType", "optional", "minExclusive", "maxExclusive", "allowedRanges", "conditionalRanges", "integer", "note", "constraints", "editor", "minItems", "maxItems", "itemType", "itemControls", "initialization"):
            if attribute in source:
                row[attribute] = source[attribute]
        exception = source.get("sourceFieldException", "")
        if "UI-only" in exception or "Host selection" in exception or "Host view selection" in exception:
            row["uiOnly"] = True
        # Saved Figma uses the implementation's local argument name `e`.
        # The host adapter exposes the public FilterEdge argument as `edge`.
        # Preserve the original node contract and name the SDK binding explicitly.
        if panel["api"] in {"shear", "offset"} and source["parameter"] == "e":
            row["sdkParameter"] = "edge"
        if "[]" in source["parameter"]:
            row["collection"] = {"path": panel.get("sourceParameter", source["parameter"].split("[]", 1)[0]), "index": panel.get("collectionIndex", 0), "existingItem": panel.get("existingItem", False)}
        initial = source.get("numericValue", source.get("default", source.get("value", "")))
        if source["kind"] == "number":
            row.update(type="field", control="slider", initial=displayed_value(str(initial)) if isinstance(initial, str) else initial,
                       min=source["min"], max=source["max"], unit=source.get("unit", ""), step=source.get("step", 0.01 if source["max"] - source["min"] <= 10 else 1), displayValue=source.get("value", str(initial)))
        elif source["kind"] == "toggle":
            row.update(type="field", control="toggle", initial=bool(source.get("checked", initial)))
        elif source["kind"] == "choice":
            values = source.get("optionValues", source.get("enumValues", source["options"]))
            selected = source.get("selected", 0)
            row.update(type="choices", options=[], selectedIndex=selected, initial=values[selected])
            for index, label in enumerate(source["options"]):
                option = {"label": label, "value": values[index]}
                if index < len(source.get("optionApis", [])):
                    option["api"] = source["optionApis"][index]
                elif source.get("api"):
                    option["api"] = source["api"]
                option_targets = source.get("optionTargets", [])
                if index < len(option_targets) and option_targets[index] in panel["nameIds"]:
                    option["target"] = panel["nameIds"][option_targets[index]]
                row["options"].append(option)
        elif source["kind"] == "action":
            row.update(type="action", initial=initial, options=[{"label": source.get("buttonLabel", source["label"]), "value": source.get("action", "edit"), "api": source.get("api", panel["api"])}])
            target_name = source.get("target")
            if target_name in panel["nameIds"]:
                row["options"][0]["target"] = panel["nameIds"][target_name]
                row["target"] = panel["nameIds"][target_name]
        elif source["kind"] == "text":
            row.update(type="field", control="text", initial=initial, uiOnly=True)
        else:
            raise ValueError("Unexpected ledger source control: " + source["kind"])
        if panel["tool"] == "color" and source["parameter"] in LEGACY_COLOR and panel["name"] in {"Color_Light", "Color_WB", "Color_HSL"}:
            row["legacyField"] = LEGACY_COLOR[source["parameter"]]
        return row
    destination = targets(node)
    if destination:
        row["target"] = destination[0]
    if kind in {"Parameter", "Input", "Toggle", "Choice"}:
        row.update(type="field", parameter=key)
        if kind == "Parameter":
            row.update(control="slider", initial=displayed_value(texts[1] if len(texts) > 1 else "0"))
            row.update(numeric_contract(texts[-1], row["initial"]))
        elif kind == "Toggle":
            row.update(control="toggle", initial=displayed_value(texts[1] if len(texts) > 1 else "false"))
        elif kind == "Choice":
            row.update(control="select", initial=texts[1] if len(texts) > 1 else "", options=texts[1:])
        else:
            row.update(control="text", initial=displayed_value(texts[1] if len(texts) > 1 else ""))
            if isinstance(row["initial"], (int, float)) and not isinstance(row["initial"], bool):
                row["control"] = "number"
        if panel["tool"] == "color" and key in LEGACY_COLOR and panel["name"] in {"Color_Light", "Color_WB", "Color_HSL"}:
            row["legacyField"] = LEGACY_COLOR[key]
        return row
    if kind == "Actions":
        row.update(type="action", label="", options=[])
        for child in children(node):
            label = (node_texts(child) or [child.get("name", "")])[0]
            if not label:
                continue
            option = {"label": label, "value": label.lower(), "id": child.get("id", "")}
            if targets(child):
                option["target"] = targets(child)[0]
            row["options"].append(option)
        return row
    if kind == "Tools":
        row.update(type="gallery", label="", options=[])
        for child in children(node):
            label = semantic(child)[1] or (node_texts(child) or [child.get("name", "")])[0]
            option = {"label": label, "id": child.get("id", ""), "icon": icon_name(child, context)}
            if targets(child):
                option["target"] = targets(child)[0]
            row["options"].append(option)
        return row
    if kind == "Section":
        row.update(type="section", label=key, rows=[])
        for child in children(node):
            if semantic(child)[0] in SEMANTIC:
                row["rows"].append(make_row(child, panel, context, source_controls))
            elif child.get("type") != "TEXT":
                row["rows"].append(make_row(child, panel, context, source_controls))
        return row
    if "Curve plot" in node.get("name", "") or "curve plot" in node.get("name", "") or node.get("name", "").startswith("Tone curve ·"):
        row.update(type="curve", label="Curve", points=[[0, 0], [1, 1]], icon="DreamscapesAdjustCurvePlot")
        return row
    if destination:
        row.update(type="action", label=texts[0] if texts else node.get("name", ""), options=[{"label": texts[0] if texts else node.get("name", ""), "target": destination[0]}])
        return row
    row.update(type="section", rows=[], label=texts[0] if texts else node.get("name", ""))
    return row


def flatten_controls(controls):
    for control in controls:
        if control["kind"] == "section":
            yield from flatten_controls(control["controls"])
        elif control["kind"] != "note" and control.get("action") != "applyEffect":
            yield control


def source_frame(spec, stored, name_ids):
    """Reproduce the saved author's render-panels.js Auto Layout contract.

The ledger records real IDs for interactive controls and tiles. Non-interactive
wrapper IDs were not retained and receive explicitly generated IDs instead.
"""
    row_controls = collections.defaultdict(list)
    for control in stored["controls"]:
        row_controls[(control["kind"], control["parameter"], control["name"])].append(control)
    navigation = collections.defaultdict(list)
    for entry in stored["navigation"]:
        navigation[entry["target"]].append(entry)
    def node(identifier, name, height, width=398, texts=None, nodes=None, target=None, icon=None):
        result = {"id": identifier, "name": name, "height": height, "width": width, "type": "FRAME"}
        if texts is not None:
            result["texts"] = texts
        if nodes is not None:
            result["children"] = nodes
        if target:
            result["reactions"] = [{"action": {"destinationId": target}}]
        if icon:
            result["icon"] = icon
        return result
    def build_control(control, path):
        kind = control["kind"]
        if kind == "section":
            nested = [node(stored["id"] + "/" + path + "/label", control["label"], 13, texts=[control["label"]])]
            nested[0]["type"] = "TEXT"
            nested += [result for index, child in enumerate(control["controls"]) if (result := build_control(child, path + "/" + str(index))) is not None]
            height = sum(child["height"] for child in nested) + max(0, len(nested) - 1) * 8
            return node(stored["id"] + "/section/" + path, "Section · " + control["label"], height, nodes=nested)
        if kind == "note" or control.get("action") == "applyEffect":
            return node(stored["id"] + "/note/" + path, control.get("label", control.get("value", "")), 11, texts=[control.get("label", "")])
        signature = (kind, control["parameter"], control["label"])
        if not row_controls[signature]:
            if spec["key"] == "Color_PointColor" and signature == ("action", "points", "Manage samples"):
                return None
            raise ValueError(spec["key"] + ": missing source ledger row: " + repr(signature))
        record = row_controls[signature].pop(0)
        identifier = record["id"]
        if kind == "number":
            return node(identifier, "Parameter · " + control["parameter"], 63, texts=[control["label"], str(control.get("value", "")), str(control["min"]) + " … " + str(control["max"]) + " " + control.get("unit", "")])
        if kind == "choice":
            option_rows = (len(control["options"]) + 1) // 2
            return node(identifier, "Choice · " + control["parameter"], 13 + option_rows * 22 + option_rows * 8, texts=[control["label"]] + control["options"])
        if kind == "text":
            return node(identifier, "Text · " + control["parameter"], 43, texts=[control["label"], str(control.get("value", ""))])
        return node(identifier, ("Toggle" if kind == "toggle" else "Input") + " · " + control["parameter"], 22, texts=[control["label"], str(control.get("value", ""))])
    nav_nodes = []
    for tab in spec.get("tabs", []):
        entry = navigation[tab["key"]].pop(0)
        nav_nodes.append(node(entry["id"], tab["label"], 22, 22, target=None if entry.get("currentView") else entry["targetId"], icon=ICON_NAMES[tab["icon"]]))
    nodes = [node(stored["id"] + "/header", "Header · Icon navigation", 22, nodes=[node(stored["id"] + "/nav", "Icon views", 22, nodes=nav_nodes)]), node(stored["id"] + "/title", spec["family"] + " · " + spec["label"], 13, texts=[spec["family"] + " · " + spec["label"]])]
    if spec.get("description"):
        nodes.append(node(stored["id"] + "/description", spec["description"], 11, texts=[spec["description"]]))
    if spec.get("tiles"):
        tiles = []
        for tile in spec["tiles"]:
            entry = navigation[tile["key"]].pop(0)
            tiles.append(node(entry["id"], "Tool · " + tile["label"], 51, (398 - 16) / 3, texts=[tile["label"]], target=entry["targetId"], icon=ICON_NAMES[tile["icon"]]))
        row_count = (len(tiles) + 2) // 3
        nodes.append(node(stored["id"] + "/gallery", "Tools · Icon gallery", row_count * 51 + max(0, row_count - 1) * 8, nodes=tiles))
    if "Curve" in spec["label"] and (spec["api"] == "developCurve" or "PointCurve" in spec["key"]):
        nodes.append(node(stored["id"] + "/curve", "Tone curve · neutral preview", 160))
    if spec.get("editor") in {"rgbColor", "rgbaColor"}:
        nodes.append(node(stored["id"] + "/swatch", "Selected color · " + spec.get("sourceParameter", "input"), 32))
    nodes += [result for index, control in enumerate(spec["controls"]) if (result := build_control(control, str(index))) is not None]
    footer = []
    for action, label, identifier in (("back", "Filters" if spec["family"] == "Effects" else "Overview", stored["backId"]), ("preview", "Preview", stored["previewId"]), ("apply", "Applying…" if spec.get("status") == "processing" else "Apply", stored["applyId"])):
        footer.append(node(identifier, label, 22, texts=[label], target=stored["footerTargets"].get(action)))
    nodes.append(node(stored["id"] + "/footer", "Actions · Preview Reset Apply", 22, nodes=footer))
    # Four saved caption nodes wrap to two lines. Their measured frame height
    # determines the extra line; no numeric/control geometry is stretched.
    measured = 34 + sum(child["height"] for child in nodes) + max(0, len(nodes) - 1) * 12
    wrapped = stored["bounds"]["height"] - measured
    if wrapped:
        notes = [child for child in nodes if "/note/" in child["id"]]
        if wrapped != 11 or not notes:
            raise ValueError(spec["key"] + ": saved Auto Layout height differs: " + str(wrapped))
        notes[0]["height"] += wrapped
    return node(stored["id"], spec["key"], stored["bounds"]["height"], 432, nodes=nodes)


def compile_catalog(harvest, sdk_catalog, design_source):
    inventory = read_json(harvest / "inventory.json")
    specs = {state["key"]: state for state in read_json(design_source / "expanded-states.json")["states"]}
    main_specs = read_json(design_source / "main-states.json")["states"]
    for state in main_specs:
        for index, control in enumerate(state["controls"]):
            if control["kind"] == "text" and "parameter" not in control:
                control["parameter"] = str(index) + "/" + control["label"]
            if control.get("parameter") == "image":
                control["sourceArgument"] = True
    specs.update({state["key"]: state for state in main_specs})
    ledger = {panel["key"]: panel for panel in read_json(design_source / "state.json")["panels"]}
    frames = [entry for entry in inventory if entry[2] == "FRAME"]
    if len(frames) != sum(COUNTS.values()):
        raise ValueError("Expected precisely 381 frame views")
    overview_names = {spec["key"] for spec in main_specs}
    overview_entries = [[ledger[name]["id"], name, "FRAME", ledger[name]["bounds"]["width"], ledger[name]["bounds"]["height"]] for name in sorted(overview_names)]
    all_entries = frames + overview_entries
    name_ids = {entry[1]: entry[0] for entry in all_entries}
    panels, evidence_count, unavailable_contexts = [], 0, []
    for identifier, name, _, width, height, *_ in all_entries:
        raw_path = harvest / "frames" / (identifier.replace(":", "-") + ".json")
        raw = read_json(raw_path) if raw_path.exists() else source_frame(specs[name], ledger[name], name_ids)
        if isinstance(raw, dict) and "node" in raw:
            raw = raw["node"]
        context_path = harvest / "contexts" / (identifier.replace(":", "-") + ".txt")
        context = context_path.read_text(encoding="utf-8") if context_path.exists() else ""
        evidence = jsx_evidence(context)
        evidence_count += int(evidence is not None)
        if evidence is None and name not in overview_names:
            unavailable_contexts.append(identifier)
        tool = name.split("_", 1)[0].lower()
        spec, stored = specs[name], ledger[name]
        controls_by_signature = collections.defaultdict(list)
        for control in flatten_controls(spec["controls"]):
            controls_by_signature[(control["kind"], control["parameter"], control["label"])].append(control)
        source_controls = {}
        for control in stored["controls"]:
            signature = (control["kind"], control["parameter"], control["name"])
            if not controls_by_signature[signature]:
                raise ValueError(name + ": ledger control absent from typed source: " + repr(signature))
            source_controls[control["id"]] = controls_by_signature[signature].pop(0)
        source_only = [control for remaining in controls_by_signature.values() for control in remaining]
        if source_only and not (name == "Color_PointColor" and len(source_only) == 1 and source_only[0]["label"] == "Manage samples"):
            raise ValueError(name + ": typed control absent from node ledger")
        panel = {"tool": tool, "id": identifier, "name": name, "title": spec["label"], "width": width, "height": height, "api": spec["api"], "operation": spec["api"], "rows": [], "navigation": [], "nameIds": name_ids}
        if "command" in spec:
            panel["sourceCommand"] = spec["command"]
        for attribute in ("category", "sourceType", "optionsType", "developField", "sourceParameter", "collectionIndex", "existingItem", "requires", "constraints", "recipeEnum", "status", "editor"):
            if attribute in spec:
                panel[attribute] = spec[attribute]
        panel["presentation"] = "overview" if name in overview_names else "status" if "status" in spec else "category" if "_Category_" in name else "editor"
        if source_only:
            panel["availableEditors"] = [{"label": control["label"], "parameter": control["parameter"], "target": name_ids[control["target"]], "initial": control["default"], "editor": control["editor"], **{attribute: control[attribute] for attribute in ("itemType", "itemControls", "minItems", "maxItems", "requires", "initialization") if attribute in control}} for control in source_only]
        if spec.get("parentKey") in name_ids:
            panel["parent"] = name_ids[spec["parentKey"]]
        for child in children(raw):
            if child.get("name", "").startswith("Header"):
                views = next((node for node in children(child) if node.get("name") == "Icon views"), None)
                if views:
                    for view in children(views):
                        target = targets(view)
                        panel["navigation"].append({"label": view.get("name", ""), "target": target[0] if target else identifier, "icon": icon_name(view, context), "selected": not target})
                continue
            panel["rows"].append(make_row(child, panel, context, source_controls))
        del panel["nameIds"]
        row_ids = {row["id"] for row in flatten_rows(panel["rows"])}
        if set(source_controls) - row_ids:
            raise ValueError(name + ": typed controls missing in live frame: " + repr(set(source_controls) - row_ids))
        valid_ids = set(name_ids.values())
        for row in flatten_rows(panel["rows"]):
            if source_only and row.get("parameter") == "points" and row["type"] == "choices":
                row["editorTarget"] = name_ids[source_only[0]["target"]]
            if row.get("target") not in valid_ids:
                if row.get("target"):
                    row["sourceTarget"] = row.pop("target")
            for option in row.get("options", []):
                if panel.get("status") in {"empty", "processing"} and option.get("id") == stored["applyId"]:
                    option["enabled"] = False
                if option.get("target") and option["target"] not in valid_ids:
                    option["sourceTarget"] = option.pop("target")
                    option["value"] = "overview"
        if evidence:
            for row in flatten_rows(panel["rows"]):
                if row["figmaNode"] and row["figmaNode"] not in evidence["nodes"]:
                    raise ValueError(name + ": semantic row absent from high fidelity context: " + row["figmaNode"])
                if row["id"] in source_controls and row["label"] not in evidence["texts"]:
                    raise ValueError(name + ": typed label differs from high fidelity context: " + row["label"])
            panel["evidence"] = {"contextSha256": evidence["sha256"], "highFidelity": True, "assets": [{"constant": constant, "file": file} for constant, file in evidence["assets"]]}
        else:
            panel["evidence"] = {"highFidelity": False}
        panels.append(panel)
    by_name = {panel["name"]: panel for panel in panels}
    statuses = {tool: {panel["status"]: panel["id"] for panel in panels if panel["tool"] == tool and panel["presentation"] == "status"} for tool in COUNTS}
    return {"schemaVersion": 1, "fileKey": FILE_KEY, "sdk": "iiGraphicAdjust 0.4.0", "defaults": {tool: by_name[name]["id"] for tool, name in DEFAULT_NAMES.items()}, "statusViews": statuses, "counts": COUNTS, "source": {"expandedStatesSha256": hashlib.sha256((design_source / "expanded-states.json").read_bytes()).hexdigest(), "stateLedgerSha256": hashlib.sha256((design_source / "state.json").read_bytes()).hexdigest()}, "evidence": {"highFidelityContexts": evidence_count, "unavailableContextIds": unavailable_contexts}, "panels": [panel for panel in panels if panel["name"] not in overview_names], "overviews": [panel for panel in panels if panel["name"] in overview_names]}


def flatten_rows(rows):
    for row in rows:
        yield row
        yield from flatten_rows(row.get("rows", []))


def javascript(catalog):
    data = json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
    return ".pragma library\n// Generated by tools/generate_graphic_panels.py; edit the harvest or generator.\nvar graphicCatalog = " + data + ";\nvar operationDefaultsCache = Object.create(null);\n" + r'''
function normalizedTool(tool) { return tool === "masking" ? "mask" : tool === "auto-enhance" ? "adjust" : tool }
function catalog() { return graphicCatalog }
function panelsFor(tool) { return graphicCatalog.panels.filter(function(panel) { return panel.tool === normalizedTool(tool) }) }
function byId(identifier) {
    var all = graphicCatalog.panels.concat(graphicCatalog.overviews)
    for (var index = 0; index < all.length; ++index) {
        var panel = all[index]
        if (panel.id === identifier || panel.name === identifier) return panel
    }
    return null
}
function overviewFor(tool) {
    for (var index = 0; index < graphicCatalog.overviews.length; ++index) {
        if (graphicCatalog.overviews[index].tool === normalizedTool(tool)) return graphicCatalog.overviews[index]
    }
    return null
}
function hasDocumentDefault(row) {
    if (!row.parameter || row.parameter.indexOf("[]") >= 0 || row.initial === undefined || row.uiOnly || row.sourceArgument || row.type === "curve") return false
    if (row.editor === "hostColorPicker" || row.editor === "hostImagePicker") return false
    if (row.editor === "imageInput" && typeof row.initial === "string") return false
    return true
}
function defaultsFor(identifier) {
    var panel = byId(identifier) || byId(graphicCatalog.defaults[normalizedTool(identifier)])
    var values = {}
    function visit(rows) {
        rows.forEach(function(row) {
            if (hasDocumentDefault(row)) values[row.sdkParameter || row.parameter] = row.initial
            if (row.rows) visit(row.rows)
        })
    }
    if (panel) visit(panel.rows)
    return values
}
function operationDefaults(operation) {
    if (operationDefaultsCache[operation] !== undefined) return operationDefaultsCache[operation]
    var values = {}
    function visit(rows) {
        rows.forEach(function(row) {
            var parameter = row.sdkParameter || row.parameter
            if (hasDocumentDefault(row) && values[parameter] === undefined) values[parameter] = row.initial
            if (row.rows) visit(row.rows)
        })
    }
    graphicCatalog.panels.forEach(function(panel) { if (panel.operation === operation) visit(panel.rows) })
    operationDefaultsCache[operation] = values
    return values
}
function definition(tool) {
    var panel = byId(graphicCatalog.defaults[normalizedTool(tool)])
    if (!panel) return null
    return {key: tool, name: normalizedTool(tool) === "mask" ? "Mask" : normalizedTool(tool).charAt(0).toUpperCase() + normalizedTool(tool).slice(1), title: panel.title, figmaNode: panel.id, fields: []}
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from-manifest", type=Path, help="Reuse a reviewed manifest when raw Figma harvest files are unavailable")
    parser.add_argument("--harvest", type=Path, default=ROOT / "build/figma-editor-panels")
    parser.add_argument("--sdk-catalog", type=Path, default=ROOT.parents[1] / "SDK/iiGraphicAdjust/docs/filter-catalog.json")
    parser.add_argument("--design-source", type=Path, default=ROOT.parents[1] / "Design/Dreamscapes/figma-adjust-filter-20261008")
    parser.add_argument("--output", type=Path, default=ROOT / "src/App/Views/Editor/GraphicPanels.json")
    parser.add_argument("--native-catalog", type=Path, default=ROOT / "resources/viewmodels/graphic.json")
    parser.add_argument("--javascript", type=Path, help="Optional legacy parity fixture; never a production resource")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = read_json(args.from_manifest) if args.from_manifest else compile_catalog(args.harvest, args.sdk_catalog, args.design_source)
    outputs = {args.output: json.dumps(catalog, ensure_ascii=False, indent=2) + "\n",
               args.native_catalog: json.dumps(catalog, ensure_ascii=False, separators=(",", ":")) + "\n"}
    if args.javascript:
        outputs[args.javascript] = javascript(catalog)
    for path, contents in outputs.items():
        if args.check:
            actual = path.read_text(encoding="utf-8") if path.exists() else None
            matches = actual == contents
            if actual is not None and path == args.native_catalog:
                matches = json.loads(actual) == catalog
            if not matches:
                raise SystemExit("Generated output differs: " + str(path))
        else:
            path.write_text(contents, encoding="utf-8")
    print(json.dumps({"views": len(catalog["panels"]), "counts": COUNTS, "evidence": catalog["evidence"]["highFidelityContexts"]}))


if __name__ == "__main__":
    main()
