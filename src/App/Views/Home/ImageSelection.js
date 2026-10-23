.pragma library

function cancelOwnedDownload(download, fileKey, containerId) {
    if (!download || !download.active || !fileKey || !containerId) return false
    const state = download.state || ({})
    if (state.key !== fileKey || state.containerId !== containerId) return false
    download.cancel()
    return true
}

function source(entry) {
    return entry ? String(entry.documentSource || entry.source || entry.path || entry.imageSource || entry.mediaSource || "") : ""
}
function key(entry) {
    return entry && entry.containerId && entry.key ? String(entry.containerId) + "|" + String(entry.key) : source(entry)
}
function isImage(entry) {
    return entry && entry.mediaType !== "Video" && entry.mediaType !== "Canvas"
        && (entry.isImage !== false || !!entry.documentSource)
        && key(entry).length > 0 && (!!entry.documentSource || !/\.iiscp?(?:\?.*)?$/i.test(source(entry)))
}

// Usage wrappers are an auxiliary native representation while the original
// historical image still exists. Keep one selectable history entry per image.
function historyImages(entries) {
    const originals = {}
    entries.forEach(entry => { if (entry.path) originals[String(entry.path)] = true })
    return entries.filter(entry => {
        const path = String(entry.path || "")
        if (!/\.(png|jpg|jpeg|webp|bmp|tif|tiff)\.[0-9a-f]{64}\.iisc$/i.test(path)) return true
        return !originals[path.replace(/\.[0-9a-f]{64}\.iisc$/i, "")]
    })
}
