# Mobile navigation icon contract

The toolbar uses the exact, unmodified SVG exports from Dreamscapes Figma
`bn8O4AHKr1X9DWnhR1TgEy`, node `103:1211` (2026-09-28).
Assets live in `src/App/Views/Home/Assets/Navigation/` and are compiled into the
application resource collection, so installed devices never fetch Figma URLs.

| Slot | Figma source | Local SVG | Artwork dimensions |
| --- | --- | --- | --- |
| Home | `.MobileNavigation/Icon/home-1` | home.svg | 24 × 24 |
| Tools | `collection` | tools.svg | 24 × 24 |
| Storage | `sqlFile` / Database | storage.svg | 17.5 × 20.5545 |
| Notification | `.MobileNavigation/Icon/toolwindownotifications` | notification.svg | 24 × 24 |
| Account | `role` (Light variant) | account.svg | 24 × 24 |
| Search | `.MobileNavigation/Icon/inputFieldSearch` | search.svg | 24 × 24 |

The navigation remains `LV.MobileNavigationBar`. Each entry opts into
`preserveIconColors`; the existing generic monochrome mode stays the LVRS default.
Storage uses its exported artwork dimensions centered in the same 24px icon slot,
rather than stretching the database glyph to the full slot. No SVG geometry or
root dimensions are rewritten. Home's selected blue and the other icons' original
colors/opacity are baked into their Figma exports; Home uses `autoSelect: false`.

Regression: `DreamscapesGuiTests mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation`
checks all five tab sources, non-empty assets, image loading, painted dimensions,
source-color preservation, and the separate search source. Set
`DREAMSCAPES_MOBILE_HOME_CAPTURE` to capture the host-rendered mobile screen.
`python3 -B tests/test_mobile_navigation_assets.py` locks the six export hashes,
SVG root dimensions, source callsites, and resource registration.
LVRS `LVRSTests_mobile_navigation source_icon_colors_and_artwork_geometry` tests
the reusable properties, unchanged defaults, search forwarding, and disabled alpha.
Host screenshots are not physical-device installation evidence; separately verify
the signed iOS bundle, device installation, and process launch.

## Verification — 2026-09-28

- LVRS mobile navigation tests: 7 passed, 0 failed; the optional gallery capture
  test was skipped. Both macOS and iOS LVRS libraries were rebuilt and installed.
- Exact SVG asset contract: passed for all six exports.
- Rebuilt Dreamscapes GUI regression: 3 passed, 0 failed, 0 skipped, including
  the five tab image loading/dimension assertions and separate search source.
- Dreamscapes macOS build and bundle signing completed before switching the
  canonical `build/bin/Dreamscapes.app` output to iOS.
- The iOS dependency installation was refreshed to iiSocietyContainer 0.14.1
  and iiSocietyHelper 0.7.2 without changing their source in this task.
- Release iOS build succeeded with runtime probes disabled. The complete
  `tests/verify_ios_bundle.py` check passed for the paired iPhone 15 Pro Max.
- Updated the existing `com.iisacc.dreamscapes` installation without uninstalling
  it, launched it successfully, and inspected its physical-device screenshot.
  All six toolbar icons retain the exported shapes, colors, and proportions.

Local evidence under `build/`: `mobile-toolbar-gui-test.log`,
`mobile-toolbar-ios-build.log`, `mobile-toolbar-ios-verification.json`,
`mobile-toolbar-install.json`, `mobile-toolbar-after-launch.json`, and
`mobile-toolbar-after-app.png`. `mobile-toolbar-before-app.png` is the actual
pre-update device screenshot; `mobile-toolbar-host.png` is only a host render.
This verification covers the toolbar and app installation/launch, not an image
generation or storage synchronization end-to-end test.
