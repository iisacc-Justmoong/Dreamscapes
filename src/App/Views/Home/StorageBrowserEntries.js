.pragma library

// Consume the owner's complete Alias snapshot; Home's recent lists are capped.
function sectionFiles(aliases, section) {
    const rows = aliases.filter(row => row.section === section && row.exists)
    const byPath = Object.create(null)
    rows.forEach(row => { byPath[row.relativePath] = row })
    return rows.filter(row => {
        const relative = String(row.relativePath || "")
        if (section === "generation-history" && /\.(png|jpg|jpeg|webp|bmp|tif|tiff)\.[0-9a-f]{64}\.iisc$/i.test(relative)
            && byPath[relative.replace(/\.[0-9a-f]{64}\.iisc$/i, "")]) return false
        return !/-poster\.png$/i.test(relative) || !byPath[relative.replace(/-poster\.png$/i, "-0001.mp4")]
    }).map(row => {
        const file = Object.assign({}, row)
        const name = String(file.name || "")
        if (/\.(mp4|mov|m4v|webm|mkv)$/i.test(name)) {
            file.mediaType = "Video"
            file.mediaSource = file.source || ""
            const poster = byPath[String(file.relativePath).replace(/-0001\.mp4$/i, "-poster.png")]
            if (poster && poster !== row) file.previewSource = poster.previewSource || ""
            file.imageSource = file.previewSource || ""
        } else if (/\.iiscp?$/i.test(name)) file.mediaType = "Canvas"
        return file
    })
}

function listing(files, section, directory, query, sortOrder) {
    const prefix = directory ? directory + "/" : ""
    const needle = query.trim().toLocaleLowerCase()
    const folders = Object.create(null)
    const result = []
    files.forEach(file => {
        const relative = String(file.relativePath || "")
        if (!relative.startsWith(prefix)) return
        const tail = relative.slice(prefix.length)
        if (needle) {
            if (tail.toLocaleLowerCase().indexOf(needle) >= 0) result.push(file)
        } else if (tail.indexOf("/") >= 0) {
            const name = tail.slice(0, tail.indexOf("/"))
            if (!folders[name]) {
                const path = prefix + name
                folders[name] = true
                result.push({key: "directory:" + section + "/" + path, name: name,
                    relativePath: path, isDirectory: true, available: true})
            }
        } else result.push(file)
    })
    result.sort((a, b) => {
        if (!!a.isDirectory !== !!b.isDirectory) return a.isDirectory ? -1 : 1
        if (!a.isDirectory && sortOrder !== "name") {
            const left = new Date(a.modified).getTime() || 0
            const right = new Date(b.modified).getTime() || 0
            if (left !== right) return sortOrder === "oldest" ? left - right : right - left
        }
        return String(a.name).localeCompare(String(b.name))
    })
    return result
}
