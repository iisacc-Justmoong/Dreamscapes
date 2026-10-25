.pragma library

function choices(key, options, label) { return { kind: "choices", key: key, options: options, label: label || "" } }
function toggles(entries) { return { kind: "toggles", entries: entries } }
function fields(entries) { return { kind: "fields", entries: entries } }
function select(key, label, options) { return { kind: "select", key: key, label: label, options: options } }
function section(label, rows) { return { label: label, rows: rows } }
function appearance() { return fields([["color", "Color", ""], ["opacity", "Opacity", "%"]]) }
function sections(family, value) {
    const preview = section("", [{ kind: "preview" }])
    if (family === "snap") return [
        section("", [toggles([["enabled", "Enable snapping"]])]),
        section("", [choices("preset", ["Smart", "Pixel", "Vector", "Layout"])]),
        preview,
        section("Snap to", [
            toggles([["canvas-edges", "Canvas edges"], ["canvas-centers", "Canvas centers"]]),
            toggles([["layer-edges", "Layer edges"], ["layer-centers", "Layer centers"]]),
            toggles([["guides", "Guides"], ["grid", "Grid"]]),
            toggles([["pixel-edges", "Pixel edges"], ["pixel-centers", "Pixel centers"]]),
            toggles([["anchors", "Path anchors"], ["baselines", "Text baselines"]]),
            toggles([["spacing", "Equal spacing"], ["size", "Equal size"]])
        ]),
        section("Detection · screen pixels", [
            fields([["tolerance", "Tolerance", " px"], ["release", "Release", " px"]]),
            choices("priority", ["Nearest", "Guides first", "Grid first"], "Priority"),
            choices("axes", ["Both", "Horizontal", "Vertical"], "Axes")
        ]),
        section("Transform increments", [
            toggles([["angle", "Angle snap"], ["scale", "Scale snap"]]),
            fields([["angle-step", "Angle", "°"], ["scale-step", "Scale", "%"]])
        ]),
        section("Feedback and scope", [
            toggles([["smart-guides", "Smart guides"], ["distances", "Distances"]]),
            toggles([["visible-only", "Visible only"], ["ignore-locked", "Ignore locked"]])
        ])
    ]
    if (family === "grid") {
        const type = value.type, unit = value.units === "px" ? "" : " " + value.units
        let geometry = []
        if (type !== "Pixel") geometry.push(choices("units", ["px", "mm", "pt", "%"], "Units"))
        if (type === "Rectangular") geometry = geometry.concat([
            fields([["spacing-x", "Spacing X", unit], ["spacing-y", "Spacing Y", unit]]),
            fields([["subdivisions", "Subdivisions", ""], ["major-every", "Major every", ""]]),
            fields([["rotation", "Rotation", "°"]])])
        if (type === "Isometric") geometry = geometry.concat([
            fields([["step", "Step", unit], ["axis-angle", "Axis angle", "°"]]),
            fields([["subdivisions", "Subdivisions", ""], ["rotation", "Rotation", "°"]])])
        if (type === "Polar") geometry = geometry.concat([
            fields([["step", "Radial step", unit], ["spokes", "Spokes", ""]]),
            fields([["start-angle", "Start angle", "°"], ["sweep", "Sweep", "°"]]),
            fields([["ring-count", "Ring count", ""]])])
        if (type === "Perspective") geometry = geometry.concat([
            select("projection", "Projection", ["1-point", "2-point"]),
            fields([["horizon", "Horizon", "%"], ["density", "Density", ""]]),
            fields([["vp1-x", "VP 1 X", "%"], ["vp1-y", "VP 1 Y", "%"]]),
            fields([["vp2-x", "VP 2 X", "%"], ["vp2-y", "VP 2 Y", "%"]]),
            fields([["depth-step", "Depth step", "%"]])])
        if (type === "Pixel") geometry = [
            fields([["pixel-step", "Pixel step", " px"], ["reveal-at", "Reveal at", "%"]]),
            { kind: "toggles", family: "snap", entries: [["pixel-edges", "Pixel edges"], ["pixel-centers", "Pixel centers"]] }]
        if (type === "Baseline") geometry = geometry.concat([
            fields([["increment", "Increment", unit], ["offset", "Offset", unit]]),
            fields([["major-every", "Major every", ""]])])
        if (type === "Layout") geometry = geometry.concat([
            fields([["columns", "Columns", ""], ["rows", "Rows", ""]]),
            fields([["gutter-x", "Gutter X", unit], ["gutter-y", "Gutter Y", unit]]),
            fields([["margin-x", "Margin X", unit], ["margin-y", "Margin Y", unit]])])
        return [
            section("", [toggles([["enabled", "Show grid"]]), select("type", "Grid type", ["Rectangular", "Isometric", "Polar", "Perspective", "Pixel", "Baseline", "Layout"])]),
            preview,
            section(type === "Pixel" ? "Pixel alignment" : "Geometry", geometry),
            section("Origin and reference", [choices("reference", ["Canvas", "Selection", "Artboard"], "Reference"), fields([["origin-x", "Origin X", unit], ["origin-y", "Origin Y", unit]])]),
            section("Appearance", [choices("style", ["Lines", "Dots"]), appearance(),
                toggles([["major", "Major lines"], ["axes", "Origin axes"]]),
                toggles([["adaptive", "Adaptive display"], ["intersections", "Intersections"]])])
        ]
    }
    const type = value.type
    const safe = ["Video safe", "Print / bleed"].indexOf(type) >= 0
    const composition = ["Rule of thirds", "Golden ratio", "Diagonals"].indexOf(type) >= 0
    const mode = function(label, v, active) { return {label: label, value: v, active: active || [v]} }
    let create = []
    if (type === "Single") {
        create = [choices("orientation", ["Horizontal", "Vertical", "Angle"], "Orientation"), choices("units", ["px", "mm", "%"], "Units"), fields([["position", "Position", ""]])]
        if (value.orientation === "Angle") create.push(fields([["angle", "Angle", "°"]]))
    }
    if (type === "Layout") create = [choices("units", ["px", "mm", "%"], "Units"),
        fields([["columns", "Columns", ""], ["rows", "Rows", ""]]),
        fields([["gutter-x", "Gutter X", ""], ["gutter-y", "Gutter Y", ""]]),
        fields([["margin-x", "Left / right", ""], ["margin-y", "Top / bottom", ""]]), toggles([["margins", "Include margins"]])]
    if (safe) {
        create = [choices("type", ["Video safe", "Print / bleed"])]
        if (type === "Video safe") create = create.concat([
            toggles([["action-safe", "Action safe"], ["title-safe", "Title safe"]]),
            fields([["action-inset", "Action inset", "%"], ["title-inset", "Title inset", "%"]])])
        else create = create.concat([fields([["bleed", "Bleed", " mm"], ["safe-margin", "Safe margin", " mm"]]), fields([["crop-inset", "Crop inset", " mm"]])])
    }
    if (composition) {
        create = [choices("type", [mode("Thirds", "Rule of thirds"), mode("Golden", "Golden ratio"), "Diagonals"])]
        if (type === "Golden ratio") create.push(fields([["ratio", "Ratio", ""]]))
        if (type === "Diagonals") create.push(fields([["angle", "Angle", "°"]]))
    }
    create.push(choices("reference", ["Canvas", "Selection", "Artboard"], "Reference"))
    return [section("", [toggles([["enabled", "Show guides"]])]),
        section("Create", [choices("type", ["Single", "Layout"]), choices("type", [mode("Safe areas", "Video safe", ["Video safe", "Print / bleed"]), mode("Composition", "Rule of thirds", ["Rule of thirds", "Golden ratio", "Diagonals"])])]),
        preview, section(type, create),
        section("Behavior", [toggles([["locked", "Lock guides"], ["snap", "Snap to guides"]]), toggles([["smart", "Smart guides"], ["all-artboards", "All artboards"]]), toggles([["clear-existing", "Clear existing on create"]])]),
        section("Appearance", [choices("style", ["Solid", "Dashed"]), appearance(), toggles([["labels", "Guide labels"], ["above", "Above artwork"]])])]
}
