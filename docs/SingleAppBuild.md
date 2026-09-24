# Single app build contract

The product uses the [workspace single-app policy](../../build-policy/README.md).
Only the canonical application bundle is generated beneath `build/`. Tests and
helpers are ordinary executables; runtime deployment and packaging work in place.
See the policy for canonical paths, platform switching and verification commands.

The opt-in `DREAMSCAPES_LOCAL_RUNTIME_PROBE` build accepts `--inspect-society-models` to record the running generation controller's model metadata and storage state in Documents/society-models-verification.json. This read-only inspection mode never enqueues generation, selects a model, or requests a model download. Check observedAt to reject stale device reports.
