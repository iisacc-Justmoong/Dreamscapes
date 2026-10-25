.pragma library

// Presentation only: consume the storage/result snapshot without reopening a
// document, changing a revision, or requesting downloads during selection.
function key(file) { return String(file && (file.id || file.key || file.path || file.source) || "") }
function kind(file) {
    if (file.mediaType) return file.mediaType
    if (/\.iiscp?$/i.test(file.name || "")) return "Canvas"
    if (/\.(mp4|mov|m4v|webm|mkv)$/i.test(file.name || "")) return "Video"
    return file.isImage ? "Image" : "Document"
}
function value(file, name) {
    if (file[name] !== undefined && file[name] !== null) return file[name]
    const settings = file.generationParameters || file.parameters || file.metadata || ({})
    return settings[name]
}
function dimensions(file) {
    const width = Number(value(file, "width")), height = Number(value(file, "height"))
    return width > 0 && height > 0 ? width + " × " + height + " px" : qsTr("Unavailable")
}
function structure(file) {
    const layers = Number(value(file, "layerCount")), pages = Number(value(file, "pageCount"))
    if (layers > 0) return pages > 0 ? qsTr("%1 pages · %2 layers").arg(pages).arg(layers) : qsTr("%1 layers").arg(layers)
    const duration = Number(value(file, "duration")), fps = Number(value(file, "fps"))
    if (duration > 0) return fps > 0 ? duration + " s · " + fps + " FPS" : duration + " s"
    return file.metadata1 || kind(file)
}
function date(file) {
    if (file.dateText) return file.dateText
    const d = new Date(file.modified || "")
    return isNaN(d.getTime()) ? qsTr("Unavailable") : Qt.formatDateTime(d, "yyyy-MM-dd")
}
function state(file) { return file.available === false ? qsTr("Not downloaded") : qsTr("Local") }
function model(file) { return String(value(file, "modelName") || value(file, "model") || qsTr("Unavailable")) }
function resultSummary(file) {
    const seed = value(file, "seed"), size = dimensions(file)
    return seed !== undefined ? size + " · " + qsTr("Seed %1").arg(seed) : size + " · " + structure(file)
}
