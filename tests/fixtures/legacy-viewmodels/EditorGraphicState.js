.pragma library

// UI drafts use the SDK's stable member paths. The adapter receives ordinary
// nested objects and arrays; view selection never becomes document state.
function pathParts(path) {
    return String(path).replace(/\[\]/g, "[0]").replace(/\[(\d+)\]/g, ".$1").split(".").filter(function(p) { return p.length })
}
function setPath(target, path, value) {
    var parts = pathParts(path)
    if (!parts.length) return
    var cursor = target
    for (var i = 0; i < parts.length - 1; ++i) {
        var key = parts[i]
        var array = /^\d+$/.test(parts[i + 1])
        if (cursor[key] === undefined || cursor[key] === null || typeof cursor[key] !== "object") cursor[key] = array ? [] : {}
        cursor = cursor[key]
    }
    cursor[parts[parts.length - 1]] = clone(value)
}
function nested(flat, overlay) {
    var result = {}
    // Parent arrays/objects are installed first, then explicit edited members.
    Object.keys(flat).sort(function(a, b) { return pathParts(a).length - pathParts(b).length }).forEach(function(key) { setPath(result, key, flat[key]) })
    if (overlay) Object.keys(overlay).sort(function(a, b) { return pathParts(a).length - pathParts(b).length }).forEach(function(key) { setPath(result, key, overlay[key]) })
    return result
}
function numberText(row, value) {
    if (value === null) return row.displayValue || "Default"
    var number = Number(value)
    var decimals = row.decimals !== undefined ? row.decimals : row.step && row.step < 1 ? Math.min(6, Math.ceil(-Math.log(row.step) / Math.LN10)) : 0
    var text = isFinite(number) ? number.toFixed(decimals).replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "") : String(value)
    return text + (row.unit ? row.unit === "%" || row.unit === "°" ? row.unit : " " + row.unit : "")
}
function parseNumber(row, text) {
    var clean = String(text).trim()
    if ((row.optional || row.initial === null) && clean.toLowerCase() === "default") return null
    if (row.unit && clean.endsWith(row.unit)) clean = clean.slice(0, -row.unit.length).trim()
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(clean)) return undefined
    var value = Number(clean)
    if (!isFinite(value) || (row.min !== undefined && value < row.min) || (row.max !== undefined && value > row.max)) return undefined
    if (row.minExclusive && value <= row.min) return undefined
    if (row.maxExclusive && value >= row.max) return undefined
    if (row.integer && Math.floor(value) !== value) return undefined
    return value
}

function clone(value) { return value === undefined ? undefined : JSON.parse(JSON.stringify(value)) }
function getPath(target, path) {
    var parts = pathParts(path), value = target
    for (var i = 0; i < parts.length; ++i) { if (value === null || value === undefined) return undefined; value = value[parts[i]] }
    return value
}
function flatten(value, prefix) {
    var out = {}
    function visit(node, path) {
        if (node !== null && typeof node === "object" && !Array.isArray(node)) Object.keys(node).forEach(function(key) { visit(node[key], path ? path + "." + key : key) })
        else out[path] = clone(node)
    }
    visit(value, prefix || "")
    return out
}
function resolvedPath(path, indices) {
    var prefix = ""
    return String(path).split("[]").map(function(part, index) {
        prefix += part
        if (index === String(path).split("[]").length - 1) return part
        var selected = indices[prefix] === undefined ? 0 : indices[prefix]
        prefix += "[" + selected + "]"
        return part + "[" + selected + "]"
    }).join("")
}
