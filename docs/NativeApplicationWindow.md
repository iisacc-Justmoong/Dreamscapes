# Native application window migration

Dreamscapes is moving from a QML-owned application window to LVUI views hosted
by iiGUI. The window boundary must not require a `QWindow`, `QQuickItem`, or
QML engine: it accepts a native widget factory and retains its resources until
the window closes. The product supplies its real view models and commands to
that factory; the window does not invent or persist application state.

## Window and event-loop contract

The primary native window supports both a standalone iiGUI event loop and an
already-running native event loop. The latter is a transition path for the
existing Qt-based nonvisual controllers; iiGUI currently supports it on macOS
and Windows. Unsupported platforms must return the iiGUI error rather than
silently displaying an incomplete screen. The standalone path is independent
of QML and does not depend on an externally owned native window.

Window operations and callbacks belong to the creating thread. The content
factory runs once after the host opens and may use the host's UI event loop.
Its LVUI widget tree and retained services survive through the deferred close
callback. A failed factory reports its original error and closes the incomplete
window; the application can then choose its own recovery. Closing can be
vetoed by the application's existing unsaved-work policy. A window or its host
must never be destroyed while iiGUI is still processing a callback.

`chooseFile` forwards file-dialog options and completion to the live iiGUI host
on the window's owner thread. A closed or unopened window rejects the request;
unsupported hosts return their own error. A headless boundary test checks those
state and thread rules without opening a window or a system dialog.

## Scope and verification

The new boundary is migration infrastructure, not a claim that the Home,
review, editor, publishing, preferences, or mobile flows have all moved. The
existing application still uses QML and LVRS until equivalent native screens,
navigation, dialogs, accessibility, and platform behavior are connected.

Tests create an isolated native window under a nonactivating test application,
mount the generated LVUI Home queue with its real view model, exercise a button
and close veto, and verify resource lifetime and failure handling. They use no
pretrained model, live Society store, or installed Dreamscapes application.
This test exercises the external event-loop path on macOS; it does not verify
standalone event-loop ownership, the Windows adapter, or mobile hosts. The full
product build and legacy tests remain separate evidence.

`Dreamscapes.NativeHomeQueueWindow` also connects this window to the product's
real generation and workspace session through `NativeHomeQueueSession`. It
uses an isolated Society fixture and a paused generation queue, checks that
live queued-job counts reach the LVUI widgets, and activates the existing
review navigation. The current controllers in that integration remain
Qt-based; the test does not instantiate a QML or Qt view.
