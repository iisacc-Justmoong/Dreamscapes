# Native application window migration

The native app's `--iisaccson-stdin` command sends a user-provided prompt to the
same-user Society daemon, waits for the resident-model readiness event, and
returns its terminal JSON response. It starts no window and does not load model
weights in the app. The app's existing GUI starts normally without this option.
The desktop package includes the native client library; the Society service and
its resident model retain their own process lifetime.

Dreamscapes is moving from a QML-owned application window to LVUI views hosted
by iiGUI. The window boundary must not require a `QWindow`, `QQuickItem`, or
QML engine: it accepts a native widget factory and retains its resources until
the window closes. The product supplies its real view models and commands to
that factory; the window does not invent or persist application state.

## Window and event-loop contract

The application shell includes the Figma title bar above every workspace. Its
LVUI controls contain an Agent draft, notifications, and an account entry. The
native macOS window retains its system window controls and reserves their
space; a custom draggable surface occupies the remaining leading area. Other
desktop hosts retain their system window frame. The title bar is part of the
shell, so navigation and license state changes cannot remove it. The Agent
draft is editable; this layout does not submit it to an agent service. The
notification control remains disabled until a notification service is connected.
The account entry opens Account preferences. Headless widget tests cover the
shared layout, navigation, drag input, and narrow-window draft sizing.

The application attaches its iiGUI window to an iiSDKCore Application. Core owns
the event loop, common worker pool, stop signal and attachment lifecycle. The native
application lifecycle registers named stop handlers on its owner thread and executes
all of them once in order, even if one fails; late registration and reentry fail.
Generation, publication and audio use separate Core serial queues; preferences use
a dedicated Core worker. Core joins work before window resources are deactivated.

Window operations and callbacks belong to the creating thread. The content
factory runs once after the host opens and may use the host's UI event loop.
Its LVUI widget tree and retained services survive through the deferred close
callback. A failed factory reports its original and nested error and closes the
incomplete window. Layout size notifications can arrive during selected-file
publication without reentering the file state. Closing can be vetoed by the
existing unsaved-work policy; never destroy a host during an iiGUI callback.

`chooseFile` forwards file-dialog options and completion to the live iiGUI host
on the window's owner thread. A closed or unopened window rejects the request;
unsupported hosts return their own error. A headless boundary test checks those
state and thread rules without opening a window or a system dialog.

## Scope and verification

The default application builds LVUI and iiGUI views without Qt, QML or LVRS.
The retired Qt application entry point and Qt lifecycle implementations are removed.
This change does not establish former-UI parity: editor, publishing authorization,
accessibility and mobile behavior still need independent verification.

The Core runtime and lifecycle tests exercise normal and failed drivers, ordered
cleanup, callback failures, cancellation, preference writes and post-stop rejection.
The native Core window test checks the borrowed event loop, open and close on
macOS; other platforms and the installed application need separate verification.
The selected-file test covers layout notification during state publication.
External-host tests cover mounting, close veto and lifetime. Tests do not run models
or modify Society. The binary check rejects Qt dependencies throughout the link graph.

`Dreamscapes.NativeHomeQueueWindow` also connects this window to the product's
real generation and workspace session through `NativeHomeQueueSession`. It
uses an isolated Society fixture and a paused generation queue, checks that
live queued-job counts reach the LVUI widgets, and activates the existing
review navigation. The current controllers in that integration remain
Qt-based; the test does not instantiate a QML or Qt view.
