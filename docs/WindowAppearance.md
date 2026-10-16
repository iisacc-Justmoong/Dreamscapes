# Window appearance

Dreamscapes uses the default `LV.ApplicationWindow` material from
[Figma 997:3](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc?node-id=997-3).
It draws a neutral `#141414` background at 66% opacity, followed by a uniform
4% application accent overlay. Its `primaryColor` is `LV.Theme.defaultPrimary`
(`#0A84FF`), so the overlay is blue. Society uses the same contract with green
`#57965C`. There are no product-name color switches in LVRS.

A live `primaryColor` change updates the overlay through the framework color
transition. Auxiliary windows follow the app theme unless explicitly colored.
The default ApplicationWindow disables radial gradients. Standalone
WindowMaterial and PanelMaterial retain their separate radial treatment.

Desktop Home exposes this shared material directly. It must not paint an
opaque full-window surface above the background. Controls, cards, and
selection feedback retain their own component surfaces. Material diffusion
remains 64 logical pixels, native background blur stays active on supported
macOS windows, and foreground/window opacity remains 1.0. Editor exposes the
same material behind its shared sidebar; its opaque work-area surface starts
at the sidebar's right edge. Switching between Home, Image, Video and Editor
keeps sidebar geometry and the content top inset stable, including fullscreen.

`DreamscapesGuiTests mainCreatesOneSharedWindow` checks the production main
window's 66% neutral tint, 4% blue overlay, disabled radial gradients, and a
live green-to-blue accent transition. A Cocoa run also samples the final Home
image to reject an opaque content layer or missing accent. LVRS tests compare
uniform composited pixels against the Figma opacities across sizes and accent
changes, and ensure that foreground content stays opaque.
