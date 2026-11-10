# LVUI style migration

Dreamscapes must consume the LVUI design system without defining a second
product palette. The existing shipping interface is still a Qt/QML application
using LVRS widgets. The LVUI native widgets cannot be substituted into that
interface merely by changing a theme import. Migration therefore has two
separate contracts: visual values used by the current view must come from the
LVUI token catalog without application overrides, and each remaining LVRS
widget must eventually be replaced by its LVUI/iiGUI counterpart while keeping
the existing interactions and view-model connections.

The current implementation must preserve encoded sRGB and alpha when passing
LVUI colors into Qt. Lookup must use canonical LVUI catalog names, reject a
missing or incorrectly typed token, and avoid introducing a product token map.
The application may cache resolved immutable values. Creative colors, image
pixels, gradients and swatches are user content and are not UI theme tokens.

Existing Figma layout and default colors, document state, control behavior and
accessibility remain unchanged. Native LVUI component parity and the complete
removal of the legacy widget renderer must be verified independently of token
lookups; a shared color bridge alone does not establish either.

## Current implementation and boundary

`Dreamscapes.Style` exposes the LVUI catalog's typed color and number lookups
to the existing QML views. It caches the resolved immutable values without
defining an application palette. Fixed shared surface, text, accent, spacing,
typography, control-size and corner-radius roles have been migrated where the LVUI value
matches the existing display.
Editable artwork colors remain independent of the application theme. Dynamic
accent behavior and other roles that do not yet have a proven equivalent are
retained rather than changed silently.

The production shell still renders LVRS QML widgets, including their icons,
typography and interaction states. Consequently this bridge is a partial
migration: it does not claim that Dreamscapes uses only LVUI components. That
requires replacing the production views with LVUI/iiGUI equivalents and
verifying the existing visual and behavioral contracts separately.

The isolated native result-review gallery now uses LVUI-owned buttons and
images and an iiGUI virtual collection. Its generated view and standalone
link test do not use LVRS or Qt. A separate C++ adapter can feed it the existing
generation projection and preserve the original action payloads. Neither the
native gallery nor that adapter has replaced the production QML window; see
[the native review session](NativeReviewSession.md) for the exact boundary.

## SDK and verification

The application requires an LVUI SDK exposing the current canonical catalog
and `iiGUI` AOT support, together with iiColorManager 0.1.1 or newer. A legacy
LVUI package with the same package version contains older surface values and
must not be selected silently. Configure-time capability checks reject it.

`Dreamscapes.LvuiStyle` compares the bridged catalog against the existing
shared theme, checks typed lookup failures and all named view references, and
creates the panel border and timeline playhead components to verify their actual
values. The shared-style
regression suite covers the migrated views and preserves user-editable color
data. These checks do not establish full native component or visual parity.
