# In-app mirrored storage browser

Dreamscapes browses its selected drive through the read-only
`iiSocietyContainer::Container` supplied by `iiSocietyHelper::FileSystem`.
Home's All files, Generation History / View all and All published actions open
the corresponding collection inside Dreamscapes. Desktop Files and History
sidebar destinations and mobile Storage, history and search destinations use
the same route. Explicit account actions may still open Society.

The browser consumes `Container.aliases`, not the capped Home lists (20 recent
files, 20 history images and 4 publications). It does not start Society, mount a
different drive or create a second storage owner. It shows every existing Alias
in the selected collection, including records whose payload is not downloaded.
The existing owner defines the mirror's coverage; this view does not independently
scan or write to the drive. Folders are derived from the paths of mirrored files;
empty directories without file Aliases are not part of this snapshot.

Files and Published support hierarchical folder navigation. Search includes
descendant paths under the current folder and is case-insensitive. Newest,
oldest and name sorting are available. The grid creates visible delegates with
a small row cache, and previews are requested only while the view is visible.
Typing and sorting operate on cached metadata without rescanning the drive.
The existing preview provider still loads visible images and checks canvas
preview versions. Refresh asks the existing owner to update the mirror. A changed drive identity clears
folder, query and selection so stale entries cannot be reused in another drive.
Hidden browsers retain their snapshot without rebuilding it. The owner's query
notifications do not rebuild the grid when its mirror revision is unchanged.

Generation History includes image, canvas and video records beyond the Home
limit. A generated MP4 and its poster form one video item, whose media source is
the MP4 and whose preview is the poster. A legacy image wrapper is suppressed
when the corresponding original image is present. Browsing preserves original
PNGs; it does not convert or rewrite them. Canvas previews use the existing
iiSharedCanvas preview provider.

Single click selects an item. Open, the folder card's Open action, double click
or Enter opens a folder or file.
Images and native canvases use the existing editor route, videos use the result
viewer, and other local files use the platform's file handler. Missing payloads
are labeled Not downloaded. Download and open, double click or Enter starts an
explicit in-process `iiSocietyHelper::FileDownload`. The browser shows byte
progress, connection/verification status, Cancel and Retry. Both Files and
Generation History use the same route. A successful download refreshes the
owner's Alias and opens the actual local file; stale card paths are never used.
Leaving the collection, changing drives or opening a different file retires the
previous open intent. Downloads do not enter the generation queue or cancel
active generation jobs. Local resident files work without account authentication.

The application directly opens its selected native `.societycontainer`. Missing
payloads use a dedicated authenticated SDK host session in the Dreamscapes
process, with bounded 256 KiB chunks and worker-thread disk/journal operations.
The SDK pins the replica revision and native read version, verifies size and
SHA-256, and atomically publishes into the selected container. It neither opens
Society nor sends local IPC, writes a shared download-request file, or waits for
another app's `client-session.lock`. Network retrieval needs a reachable host
and a valid account session; authentication or connectivity failures remain in
Dreamscapes with Retry. Explicit account-management actions remain separate.
Image selection reuses the existing multi-artboard editor flow. Non-images,
videos, native canvases and missing payloads are excluded from image selection.
The editor remains lazy until an actual file is opened. Returning from the
editor preserves the collection and folder, and browsing preserves generation
drafts and active jobs. Home returns to the existing Home view. A generation
submission opens the existing result route and retires the browser route.

## Validation

`Dreamscapes.StorageBrowser` runs real pointer input in desktop and mobile QML
layouts against isolated native Society containers. It checks collection sizes
beyond Home limits, nested paths with URL-sensitive characters, search, folder
navigation, editor opening and return, video/poster identity, live additions and
removals, compact geometry, missing payload actions, progress/cancel/retry and
drive changes. Fresh-Alias opening and abandoned open intents are checked in
desktop and mobile layouts for both Files and History. It intercepts
the `society:` scheme and requires zero external navigations.

A separate Cocoa/Metal run waits for every preview to become ready after a
compact-window reflow and checks the pixels of all six visible PNG cards.
Its evidence is `native-views-result.json` and `native-captures/storage-macos.png`.
The software renderer can still report the existing LVRS Canvas `drawImage`
diagnostic; the native pixel check verifies the application's actual Metal path.

`Dreamscapes.DesktopHome` now requires the in-app History route. Existing
Home/editor routing and image-selection tests cover retained drafts and the
shared multi-image editing behavior. The original external redirect is captured
as a failing regression before implementation. Evidence belongs in
`build/in-app-storage-20261016/`. Source GUI tests and the packaged Cocoa entry
point are separate from real mobile-device and trained-model inference checks.

The direct-download update is recorded in `build/direct-container-download-20261016/`.
SDK session tests retrieve real payloads over LAN TLS and account relay while a
different session owns the replica lease, without launching a Society app. They
also cover cancellation/resume, hash failure, drive switches and offline local
files. The sync test rejects a stale selected version before publication.
