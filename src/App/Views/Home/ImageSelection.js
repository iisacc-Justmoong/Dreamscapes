.pragma library

function source(entry) {
    return entry ? String(entry.path || entry.imageSource || entry.mediaSource || entry.previewSource || "") : ""
}
function isImage(entry) {
    return entry && entry.mediaType !== "Video" && entry.mediaType !== "Canvas"
        && source(entry).length > 0 && !/\.iiscp?(?:\?.*)?$/i.test(source(entry))
}
