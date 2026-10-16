# Editor button interaction states

An editor button's LVRS `tone` is its fixed visual role. Mouse hover, mouse or
keyboard press, click, accessibility activation and persistent selection must
not switch Default or Borderless into Primary (Figma Accent), or switch between
Default and Borderless. Each action keeps the role specified by its design.
In Background Figma `1023:70875`, Apply color, Remove background, New mask,
Apply mask, Generate replacement and Apply replacement use Default rather than
Primary. Apply removal also keeps Default through its disabled/ready transition.

The editor uses the existing LVRS default, hover and pressed surfaces. A selected
option holds its fixed tone's `backgroundColorPressed`; disabled state takes
precedence. Unselected controls return to hover while the pointer remains over
them and to default when it leaves. Button geometry, icons, focus rings and
activation timing remain owned by LVRS.

`EditorButtonState.qml` is a nonvisual state binding for existing LVRS buttons.
It binds native `checkable` and `checked` state to the accepted model and restores
that binding after native toggles. Repeated activation cannot clear a required
selection, and a rejected edit cannot leave a false selected surface. The
button's existing action remains responsible for accepting a selection. Layer
commands without an `active` state remain ordinary noncheckable actions.
Accessibility toggle actions call that same action path. Elements retains its
existing exclusive selection binding and applies the same pressed-surface rule.

The rule covers common tool choices, Elements, Text, Layers and the document
layer picker, File, Camera/Photo, Background Color/Remove/Mask/Replace, and the
shared Graphic navigation, choices and galleries. Parameter storage and document
operations are unchanged. The generic first action's index assigns a fixed
primary role; it is not a selection state.

The editable Figma Editor page (`107:1235`) follows the same axes: `Kind` is
literal, while selection variables and prototype assignments drive
`Interaction state` (`default` / `press`). Existing library hover/press variants
retain their Kind. This update adds no local design components or annotations.

Static Figma state views use the same rule as the prototype. Header tabs retain
Borderless; parameter choices and function palettes retain their source-defined
Default or Borderless role. A selected tab or option sets `Interaction state`
to `press` instead of encoding selection in `Kind`. Existing state-variable
bindings remain attached to the state axis when normalizing static views and
right-hand copies. Camera and Capture timer Off and Camera Photo mode show their
accepted default selection through the same pressed state.

`Dreamscapes.EditorButtonStatePolicy` checks every editor QML tone binding and
rejects state-dependent or imperative type changes. `Dreamscapes.EditorButtonStates`
exercises real LVRS label/icon controls through hover, press, release, repeated
selection, keyboard and accessibility activation, rejected selection, disabled
state and deselection. It also visits each editor tool and panel state selectors to
verify fixed roles and persistent pressed surfaces in the composed panels.
Camera/Photo choices and timer presets must participate in the actual native
checked-state binding. The integration test changes camera and capture timer
presets and shutter modes, verifies the pressed surface and restores Off/Photo
without starting acquisition.
