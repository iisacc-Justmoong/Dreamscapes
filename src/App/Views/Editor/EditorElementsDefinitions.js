.pragma library
.import "EditorToolDefinitions.js" as Tools

// The ten current Figma views share the existing Elements field contract.
var views = [
    {id: "rectangle", nodeId: "585:94650", tab: 1, shape: "Rectangle", fields: ["field-0", "field-3", "field-1", "field-8"]},
    {id: "ellipse", nodeId: "650:15792", tab: 1, shape: "Ellipse", fields: ["field-0", "field-3", "field-4", "field-5"]},
    {id: "polygon", nodeId: "650:15793", tab: 1, shape: "Polygon", fields: ["field-0", "field-3", "field-7", "field-9", "field-8"]},
    {id: "line", nodeId: "650:15794", tab: 1, shape: "Line / Arrow", fields: ["field-0", "field-3"]},
    {id: "fill", nodeId: "650:15795", tab: 2, fields: ["field-2", "field-16", "field-14", "field-15"]},
    {id: "stroke", nodeId: "650:15796", tab: 3, fields: ["field-10", "field-17", "field-6", "field-11", "field-12", "field-13"]},
    {id: "selection", nodeId: "650:15797", tab: 0, fields: []},
    {id: "inspect", nodeId: "662:16150", tab: 0, fields: ["field-0", "field-1", "field-16", "field-10"]},
    {id: "multiple", nodeId: "662:16151", tab: 0, fields: []},
    {id: "clear", nodeId: "662:16152", tab: 0, fields: []}
]
var shapes = [
    {id: "rectangle", name: "Rectangle", icon: "rectangle"},
    {id: "ellipse", name: "Ellipse", icon: "ellipse"},
    {id: "polygon", name: "Polygon", icon: "polygon"},
    {id: "line", name: "Line / Arrow", icon: "line"}
]
function view(id) { return views.find(function(v) { return v.id === id }) || views[0] }
function shapeView(shape) { return (shapes.find(function(v) { return v.name === shape }) || shapes[0]).id }
function field(id) { return Tools.tool("elements").fields.find(function(f) { return f.id === id }) }
function controls(id, shape) {
    if (id !== "inspect") return view(id).fields
    if (shape === "Ellipse") return ["field-0", "field-4", "field-5", "field-16", "field-10"]
    if (shape === "Polygon") return ["field-0", "field-7", "field-9", "field-8", "field-16", "field-10"]
    if (shape === "Line / Arrow") return ["field-0", "field-10", "field-17"]
    if (shape === "Vector") return ["field-0", "field-14", "field-15"]
    return view(id).fields
}
