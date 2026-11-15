# Native generation projection binding

This transition boundary supplies the existing generation presentation to an
LVUI host without QML property bindings. It does not create a window, run a
generation request, or replace the current QML application entry point.

The host owns a `WorkspaceSession`, a `GenerationProjection`, and the binding.
The session identifies the active `GenerationController`; controller jobs,
results, storage availability, inference state, preview progress, preview URL,
and preview job identity are
copied to the projection. The session supplies the current result and request
identifiers. Changes to selection made by a native view remain owned by that
view. Initial and replacement snapshots are applied atomically so observers
cannot see a mixture of two controllers. Subsequent notifications update only
their relevant fields, avoiding repeated job-list construction during previews.
Preview URL or identity changes notify interested views without recomputing
the generation presentation, which depends on preview progress rather than
the image address. A separate job-change notification lets the Home queue
avoid rebuilding its job summary for image-only updates.

Binding, source, and target must be created and used on the same Qt owner
thread. Replacing a controller disconnects its signals. Destruction or removal
of a controller clears its projected fields without erasing the session's
current result and request identifiers; destruction of the session clears all
source-owned fields. A destroyed target cannot be updated. This bridge is a
temporary backend transport: the LVUI view and its model remain Qt-free.

The native application host and remaining screens still need to be implemented
and connected before Dreamscapes can stop loading its QML interface. Automated
tests exercise the boundary using isolated resources and a stopped fake
generation queue; they do not launch the application or perform inference.
The test also connects the real review-session adapter and native queue model,
then selects and cancels queued jobs through the existing controller. It checks
that a controller switch clears the previous preview in the same snapshot.
