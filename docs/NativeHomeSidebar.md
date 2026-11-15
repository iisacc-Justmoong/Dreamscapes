# Home sidebar migration contract

The Home sidebar follows Figma node `1467:4562`. It provides Home, New Canvas,
Image, Video, Audio, Board, Tools, Files, Assets and Generation History in the
existing order, separated into the same groups. The LVUI menu items use their
authored LVUI icons and native interaction states. Selection is a navigation
state, independent of hover, press and button type.

The parent supplies the selected action and whether the compact layout is
active. The regular view includes labels; the compact view retains icon names,
accessible labels and descriptive tooltips. Both layouts keep actions usable by
pointer and keyboard, and short windows must allow the focused item to be
revealed by scrolling. The sidebar emits the same action identifiers to the
existing Home controller rather than opening files or changing generation
state itself. Translation and navigation definitions are supplied by the
application adapter.

The Qt-free sidebar model and LVUI widget now project caller-supplied entries,
use the authored icons, and keep selected, compact and interaction states
independent. Widget tests cover action delivery, external selection, compact
accessible names, hover and keyboard-focus tooltips, keyboard activation,
scroll-to-focus and retirement. The generated renderer is checked for Qt and
LVRS dependencies.

The existing product sidebar remains QML. The native widget is not connected to
the application yet. The intermediate LVUI Home composition uses an unclipped
sidebar source, and its headless test checks that a compact tooltip opens with
geometry and text clipping extending beyond the sidebar. The eventual platform
host still needs visual verification, and all production action definitions and
the original navigation route must be connected before replacing the QML view.
