# Home sidebar migration contract

The Home sidebar follows Figma node `1467:4562` and the row definitions in
`219:2630` (Frame 18). It presents Home,
then a divider, New Canvas, Image, Video, Audio, Board and Tools, then a second
divider, followed by Files, Assets and Generation History. Each row uses the
corresponding LVUI icon, an 18-point icon slot and body typography. Idle rows
show the application window background. The stock LVUI MenuItem draws the
selected, hovered and pressed states. The composed Home sidebar is 181 points
wide. Its rows and dividers span 165 points with `Metric.space8` on each side.
Below a window width of 700 points, the 48-point sidebar displays 32-point
icon-only rows. Selection is a navigation state independent of hover and press.

Editor, Publish and Preferences are not extra Home sidebar entries. Their
existing document, editor publishing and account/settings routes remain
available. Navigating to an unlisted route clears the sidebar selection rather
than rejecting the navigation.

The parent supplies the selected action, viewport size and whether the compact
layout is active. The regular view includes labels; the compact view retains icon names,
accessible labels and descriptive tooltips. Both layouts keep actions usable by
pointer and keyboard, and short windows must allow the focused item to be
revealed by scrolling. The sidebar emits the same action identifiers to the
existing Home controller rather than opening files or changing generation
state itself. Translation and navigation definitions are supplied by the
application adapter.

The Qt-free sidebar model and LVUI widget now project caller-supplied entries,
use the authored icons, and keep selected, compact and interaction states
independent. Widget tests cover transparent idle rows in both widths, LVUI
selection, hover and press feedback, action delivery, external selection,
compact accessible names, hover and keyboard-focus tooltips, keyboard
activation, scroll-to-focus and retirement. The generated renderer is checked
for Qt and LVRS dependencies.

The application mounts this native widget through the Home workspace. Its
unclipped sidebar source permits compact tooltips to extend beyond the sidebar.
Headless checks cover the product navigation definitions, row presentation,
accessibility and routes. `Dreamscapes.NativeViewApp` shrinks the mounted Home,
clicks Files in the compact sidebar, returns to Home and restores the window
width. It checks the row widths and selection during this sequence. The
standalone widget test uses the linked iiGUI package's
public headers before the workspace's private test header to avoid mixing
different renderer ABIs. Platform rendering remains a separate verification.
