# Native file inspector

The editor's File tool uses the Information, Contents and Storage sections in
`resources/viewmodels/file.json`, which records the corresponding Dreamscapes
Figma design. The native view presents every declared row without substituting
Figma's sample content. Values come from the active iiSharedCanvas document,
the transient editor frame and the connected canvas-file session. A field for
which those providers have not supplied data is labeled `Unavailable` rather
than guessed. The row set stays present even when no document is open.

The three section selectors and the File/Layers tool switch are presentation
state. They must not change, save or re-render the canvas. The inspector only
observes the active document and file session; existing editor Open and Save
commands remain the owners of file operations. Document and file changes
refresh the visible section, and frame changes update the current-frame value.
The inspector does not scan, hash or open a file while presenting these rows.
File size, storage kind and stored revision describe the last version verified
by the file worker; external changes are detected by the existing save path.

Native model and widget tests cover the complete schema, dynamic document and
file values, navigation without document edits and actual editor mounting. The
schema is broader than the current native providers; unavailable fields are not
evidence that the corresponding information is missing from an `.iisc` file.
Repeated records are currently limited to 256 entries per field, with the
omitted count displayed; pagination for larger collections is not yet connected.
