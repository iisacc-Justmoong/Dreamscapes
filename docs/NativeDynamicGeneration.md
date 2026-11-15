# Native generation view models

Dreamscapes' native views bind observable, owner-thread C++ presentation state
to LVUI and forward user edits to the model. This follows the same boundary as
Congregation: views do not own inference or document state, and a missing
service is never reported as a successful operation.

The Home composer shares a session-local `ComposerState` with its LVUI controls.
Prompt, negative prompt, media, size, count, duration, frame rate and seed
changes must update the displayed draft without writing a document or starting
generation. The generation button is unavailable until a real generation
handler is supplied. The Advanced command opens the corresponding native
Image or Video view while retaining the draft. On that command, the applicable
Home dimensions, output count, seed and Video duration and frame rate are
copied into the matching advanced draft. Reopening Advanced without changing
those Home settings preserves edits made in the advanced draft. Later changes
to an individual Home setting replace only the corresponding advanced value.

The Image and Video screens use native parameter models rather than the static
migration text. Image defaults, types, choices and validation come from
`iiLocalDiffusion::imageParameterSpecs` and `ImageParameters`; Video uses
Dreamscapes' existing `VideoDraft` edit and validation rules. A rejected
numeric edit retains the user's input and reports the validation state without
overwriting the accepted parameter. Model selection and generation submission
remain unavailable when no provider is connected. View state is separate from
files and is not persisted by merely changing a control. Reference image,
ControlNet and LoRA collections are not yet exposed in these native views.

The result route uses the existing native review `Session` and gallery. Review
selection and shortlist changes are session-local and survive navigation. The
history provider is not connected; the screen says so and starts without
fabricated results. Edit, export and preview remain unavailable without their
respective handlers and providers.

The host passes its actual content size to the native root on first mount and
on resize, so changing pages cannot collapse the interactive viewport. The
parameter list uses LVUI's measured, virtualized collection rather than nesting
an oversized retained list inside another scroll view.

The additional editor, storage, publishing and preference model seams and
their explicit integration limits are described in
[Native editor and workspace view models](NativeDynamicWorkspaces.md). Other
migrated views remain static, and the product service connections are not
established merely by those models. Tests use in-memory models and windowless
iiGUI widget trees; they do not run generation or launch the application.
