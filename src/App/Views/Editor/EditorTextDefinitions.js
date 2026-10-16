.pragma library

// The four current Figma text views share one editable text-layer state.
var roles = [
    {name: "Title", size: 26, weight: "Bold", style: 0},
    {name: "Title2", size: 22, weight: "Bold", style: 1},
    {name: "Header", size: 17, weight: "SemiBold", style: 2},
    {name: "Header2", size: 15, weight: "SemiBold", style: 3},
    {name: "Body", size: 13, weight: "Medium", style: 4},
    {name: "Description", size: 12, weight: "SemiBold", style: 5},
    {name: "Caption", size: 11, weight: "Regular", style: 6},
    {name: "Disabled", size: 11, weight: "Regular", style: 7}
]
var aliases = [
    {name: "Display", size: 22, weight: "Bold"},
    {name: "DisplaySm", size: 17, weight: "SemiBold"},
    {name: "BodyLg", size: 15, weight: "SemiBold"},
    {name: "Overline", size: 11, weight: "Regular"}
]
var usages = [
    {name: "Free Text", icon: "Text"}, {name: "Caption", icon: "AreaText"},
    {name: "Header Title", icon: "FontSize"}, {name: "Callout", icon: "Callout"},
    {name: "Footer", icon: "AlignBottom"}
]
function field(id, label, initial, type, extra) {
    return Object.assign({id: id, label: label, initial: initial, type: type || "Field"}, extra || {})
}
function numeric(id, label, initial, minimum, maximum, unit) {
    return field(id, label, initial, "Slider", {minimum: minimum, maximum: maximum, step: unit === "" ? 0.01 : 1, unit: unit || "", decimals: unit === "" ? 2 : 0})
}
function choice(id, label, initial, options) { return field(id, label, initial, "Choices", {options: options}) }
var typography = [
    choice("field-1", "Typeface", "Pretendard", ["Pretendard", "Serif", "Mono"]),
    numeric("field-2", "Font size · px", 13, 1, 512, "px"),
    choice("textWeight", "Weight", "Medium", ["Regular", "Medium", "SemiBold", "Bold"]),
    numeric("textLineHeight", "Line height", 13, 1, 1024, "px"),
    choice("textLinePreset", "Line height presets", "Token", ["Token", "100%", "120%", "150%"]),
    choice("field-3", "Horizontal alignment", "Left", ["Left", "Center", "Right", "Justify", "Leading", "Trailing"]),
    choice("textVerticalAlignment", "Vertical alignment", "Middle", ["Top", "Middle", "Bottom"]),
    choice("textDirection", "Text direction", "Auto", ["Auto", "LTR", "RTL"]),
    numeric("field-8", "Tracking", 0, -100, 100, "")
]
var layout = [
    choice("field-5", "Placement · 9 anchors", "Bottom", ["Top left", "Top", "Top right", "Left", "Center", "Right", "Bottom left", "Bottom", "Bottom right"]),
    choice("textReference", "Align relative to", "Canvas", ["Canvas", "Artboard", "Selection"]),
    field("field-9", "Auto fit width", true, "Toggle"),
    choice("textWidthMode", "Width", "Auto", ["Auto", "Fixed", "Fill"]),
    choice("textHeightMode", "Height", "Hug", ["Hug", "Fixed", "Fill"]),
    numeric("textOffsetX", "Offset X", 0, -65536, 65536, "px"),
    numeric("textOffsetY", "Offset Y", 32, -65536, 65536, "px"),
    numeric("textBoxWidth", "Fixed width", 320, 1, 65536, "px"),
    numeric("textBoxHeight", "Fixed height", 80, 1, 65536, "px"),
    choice("textHorizontalConstraint", "Horizontal constraint", "Center", ["Left", "Center", "Right", "Left + right", "Scale"]),
    choice("textVerticalConstraint", "Vertical constraint", "Bottom", ["Top", "Center", "Bottom", "Top + bottom", "Scale"]),
    numeric("field-12", "Padding", 20, 0, 256, "px"),
    numeric("field-14", "Bottom inset", 32, 0, 256, "px")
]
var appearance = [
    choice("textFill", "Text fill", "White", ["White", "Black", "Accent", "Custom"]),
    field("textCustomFill", "Custom fill", "#FFFFFF", "Color"),
    numeric("textOpacity", "Text opacity", 100, 0, 100, "%"),
    numeric("field-6", "Backdrop", 72, 0, 100, "%"),
    choice("textBlend", "Blend mode", "Normal", ["Normal", "Multiply", "Screen", "Overlay", "Soft light", "Difference"]),
    numeric("textBrightness", "Brightness", 100, 0, 200, "%"),
    numeric("textContrast", "Contrast", 100, 0, 200, "%"),
    numeric("textSaturation", "Saturation", 100, 0, 200, "%"),
    numeric("textHue", "Hue", 0, -180, 180, "°"),
    numeric("textGamma", "Gamma", 1, 0.01, 10, ""),
    numeric("textSoftness", "Softness", 0, 0, 100, "%"),
    numeric("textRangeMin", "Range min", 0, 0, 100, "%"),
    numeric("textRangeMax", "Range max", 100, 0, 100, "%"),
    choice("textRangeChannel", "Range channel", "Luminance", ["Luminance", "Alpha", "Color"]),
    numeric("textCornerRadius", "Corner radius", 12, 0, 256, "px"),
    numeric("textStrokeWidth", "Stroke width", 0, 0, 128, "px"),
    numeric("textStrokeOpacity", "Stroke opacity", 100, 0, 100, "%"),
    numeric("textBlur", "Blur", 0, 0, 128, "px"),
    choice("textStrokeColor", "Stroke color", "White", ["White", "Black", "Accent"]),
    field("textShadow", "Shadow", false, "Toggle"),
    numeric("textShadowOpacity", "Shadow opacity", 24, 0, 100, "%"),
    numeric("textShadowBlur", "Shadow blur", 12, 0, 128, "px"),
    numeric("textShadowX", "Shadow X", 0, -256, 256, "px"),
    numeric("textShadowY", "Shadow Y", 4, -256, 256, "px"),
    numeric("textShadowSpread", "Shadow spread", 0, 0, 128, "px"),
    numeric("textFeather", "Feather", 0, 0, 128, "px"),
    numeric("textNoise", "Noise amount", 0, 0, 100, "%"),
    numeric("textNoiseScale", "Noise scale", 1, 1, 128, "px"),
    numeric("textNoiseSeed", "Seed", 0, 0, 2147483647, "integer"),
    choice("textNoisePattern", "Noise pattern", "Uniform", ["Uniform", "Gaussian", "Blue noise"]),
    field("textMonochrome", "Monochrome", true, "Toggle"),
    choice("field-10", "Bubble", "Round", ["Round", "Sharp", "Cloud"]),
    choice("field-11", "Pointer · 8 directions + none", "Left", ["Top left", "Top", "Top right", "Left", "None", "Right", "Bottom left", "Bottom", "Bottom right"]),
    numeric("textPointerLength", "Pointer length", 16, 0, 256, "px"),
    numeric("textPointerWidth", "Pointer width", 12, 0, 256, "px"),
    numeric("textPointerOffset", "Pointer offset", 50, 0, 100, "%"),
    numeric("textPointerRoundness", "Pointer roundness", 0, 0, 100, "%")
]
function definition() {
    return {key: "text", title: "Text", number: 2, figmaNode: "627:95323",
        selector: choice("selector", "Usage", "Free Text", usages.map(function(u) { return u.name })),
        fields: [field("field-0", "Content", "Enter your text…"), field("textRole", "LVRS · Text role", "Body"),
            field("textLayoutEnabled", "Use placement", false, "Toggle"), field("field-4", "Time range", "00:02.4 — 00:06.8"),
            choice("field-7", "Scale", "Display", ["Display", "H1", "H2"]),
            choice("field-13", "Footer · Content", "Custom", ["Custom", "Page no.", "Metadata"])].concat(typography, layout, appearance)}
}
function byId(id) { return definition().fields.find(function(f) { return f.id === id }) }
function defaultsFor(fields) {
    var result = {}; fields.forEach(function(f) { result[f.id] = f.initial }); return result
}
function roleValues(role) { return {textRole: role.name, "field-2": role.size, textWeight: role.weight, textLineHeight: role.size, textLinePreset: "Token"} }
function readout(values) { return values.textRole + " · " + values["field-2"] + " px · " + values.textWeight }
function fillColor(value, custom) { return value === "Black" ? "#000000" : value === "Accent" ? "#0A84FF" : value === "Custom" ? custom : "#FFFFFF" }
