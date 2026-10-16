# Native color picker

Dreamscapes links the installed `iiColorManager::iiColorManager` 0.1.0 CMake
target. The Qt-free C++23 SDK owns color conversions, numeric channel ranges,
picker coordinates, dynamic ramps, web-safe quantization, gamut indication and
accept/cancel snapshots. `Views/ColorManagerModel` is the Qt GUI boundary;
`Views/ColorPicker.qml` uses the installed LVRS controls and theme.

The mode menu lists Hue Cube, Brightness Cube, Color Wheel, Grayscale Slider,
RGB Sliders, HSB Sliders, CMYK Sliders, Lab Sliders and Web Color Sliders in that
order. Mouse drags, keyboard arrows, sliders and numeric entries edit the same
native state. Alpha, hexadecimal input, previous/current previews, recent colors
and Apply/Cancel are shared across all modes. Selecting a mode does not edit a
document or quantize its selected color.

The picker header follows the title/selector row in Dreamscapes Figma
[`1023:70876`](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1023-70876),
specifically header `1031:184463`: `Color picker` on the left and a compact,
right-aligned LVRS ComboBox above the interactive picker. The selector adapts to
the selected mode's full name instead of stretching across the row. Its native
Primary/UpDown appearance and icon come from installed LVRS; no exported image
or temporary Figma URL is used at runtime. Clicking, Space or Return opens all
nine modes. The menu aligns with the selector and stays inside a narrow viewport.

The shared picker replaces the former six-mode `LV.ColorPicker` in all four
production paths: Home paint color, Editor document paint color, ordinary tool
color fields, and graphic adjustment color fields. Existing host handlers still
commit only on acceptance. The public QML `setColor`, `setHex`, `accept`, `cancel`,
`accepted`, `canceled`, and `colorEdited` contracts remain available.

The implementation refers to the recovered Photoshop color-panel/dialog
declarations under `Engineering/Projects/Adobe/Photoshop`. See the SDK's
`docs/PhotoshopEvidence.md` for source scope and its `docs/ColorModes.md` for
conversion and interaction contracts. Lab uses sRGB↔D50 CIELAB with Bradford
adaptation. CMYK is a generic model; it does not substitute for a document's ICC
press profile. This UI work does not establish Adobe native pixel parity.

Build and test with the existing `build/` tree. The `Dreamscapes.ColorManager`
test covers all nine modes at 280px and 402px, native model bindings, numeric
edits, pointer/keyboard surfaces and accept/cancel. It verifies the header and
selector geometry and chooses every mode through the real menu at both widths.
Its nine GUI captures are
written to `build/color-manager-verification/`. Existing Home/tool/graphic GUI
regressions cover the production caller paths. The real application registration
is in `src/main.cpp`, independently of the test registration.
The Home painting fixture creates an isolated Society drive for attachment
imports and restores its previous container environment on every exit. It does
not depend on the user's active drive. Packaged startup verification removes
development library/import overrides and checks the bundle's real entry point.
The packaged entry check allows 90 seconds for a cold start from the external
workspace volume; its process is still required to emit `root-loaded`, create
one window, and discover the isolated test helper. Elapsed time alone is not a
successful startup result.
