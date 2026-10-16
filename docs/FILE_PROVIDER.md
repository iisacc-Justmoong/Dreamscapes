# File creation ownership in Dreamscapes

Native AI PNG publication, generation-worker staging, image presets/preferences, editor capture/reference files and home previews use iiFileProvider. Android package assembly includes its storage bridge. Generation History preserves PNG and editor persistence preserves IISC.

The common [ownership contract](../../../SDK/iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/App/Generation/AdvancedImageParameters.cpp`
- `src/App/Generation/GenerationController.cpp`
- `src/App/Generation/GenerationController.h`
- `src/App/Generation/ModelPreferences.cpp`
- `src/App/Views/Editor/EditorCameraPhoto.cpp`
- `src/App/Views/Editor/EditorCameraPhoto.h`
- `src/App/Views/Editor/EditorToolActions.cpp`
- `src/App/Views/Home/HomeCanvas.cpp`
- `src/App/Views/Home/HomeCanvas.h`
- `src/platform/android/src/com/iisacc/dreamscapes/DreamscapesActivity.java`
