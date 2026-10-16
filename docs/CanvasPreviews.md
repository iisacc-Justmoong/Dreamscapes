# IISC previews in Continue creating

Home file cards obtain native canvas previews from iiSharedCanvas's public
`readIiscPreview` API. They display the composed document rather than feeding an
IISC path into Qt's ordinary image decoder or showing only the first raster asset.
Both `HomeContentRow` (desktop) and `FileCarousel` (mobile and the file grid) share
the `CanvasPreview` adapter. Raster files keep their existing preview URLs and
unavailable remote files retain any provider-supplied preview.

`CanvasPreview::sourceForFile` returns an `image://iisc-preview/` URL for a local
native document. Its engine-owned image provider forces asynchronous loading,
decodes/renders through iiSharedCanvas on Qt Quick's image worker, and copies the
module's straight-alpha ARGB output before its result is destroyed. Default output
is bounded to 512 by 512 and requested thumbnail bounds are capped at 2048 pixels
per axis. Aspect and transparency are preserved. Qt Quick owns the image cache;
the URL includes the main file and WAL sizes and high-resolution modification
times so a refreshed card does not reuse an earlier same-size file revision.

No canvas is opened in the editor, migrated or saved merely to show a card.
The SDK supports existing IISC snapshots and SQLite working files, including
committed WAL contents and read-only sources. Missing/corrupt/over-limit documents
return an empty image, allowing the existing LVRS card placeholder to remain.
The full source document is decoded under SDK serialization limits; bounded
thumbnail output does not imply partial decoding of large source assets.

Generation History continues to store generated PNG originals. This preview path
does not convert generation output or add sidecar thumbnails to Society Files.

Validation:

- `iiSharedCanvas.DocumentPreview` and its installed-package consumer verify
  native rendering, compatibility, bounds and source preservation.
- `Dreamscapes.CanvasPreview` verifies worker loading through QML, actual module
  pixels, alpha/aspect, special-character paths, live WAL cache invalidation and
  ordinary/unavailable image fallback.
- `Dreamscapes.HomeCanvasPreviews` loads desktop and mobile Home views with both
  native storage kinds and requires actual file cards to reach `Image.Ready`.
- `Dreamscapes.DesktopHome` and `Dreamscapes.ImageCanvas` retain Home routing,
  raster preview and PNG-to-IISC storage coverage.
