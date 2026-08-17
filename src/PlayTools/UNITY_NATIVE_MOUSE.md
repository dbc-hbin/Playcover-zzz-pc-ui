# Profile-gated Unity native mouse

PlayTools can optionally publish a virtual `Mouse` into a game's Unity
InputSystem. The feature is experimental, disabled by default, and controlled
per app from **Input Compatibility → Unity Native Mouse (Experimental)**. A
restart is required after changing it.

The bridge does not accept addresses from user settings. It only activates when
an embedded profile matches all of the following:

- application bundle identifier;
- UnityFramework Mach-O UUID;
- profile and event-encoder versions;
- reviewed 16-byte code fingerprints.

Unknown games and updated binaries remain inactive. A missing Unity image or an
InputSystem that is not ready is retried at 250 ms intervals; an actual UUID or
fingerprint mismatch permanently disables the bridge for that process before it
calls game code.

The common event path supports position, relative movement, horizontal and
vertical scroll, and five buttons (left, right, middle, forward, and back). It
uses the existing AKInterface event monitors only when PlayTools keymapping is
disabled, so the native path and fake-touch path do not process the same input.

## Adding a game profile

Add an immutable `PTUnityNativeMouseProfile` entry in
`PlayTools/Controls/NativeMouse/PTUnityNativeMouseProfiles.c`. Every RVA must be
derived from that exact UnityFramework build. Record the bundle identifier,
Mach-O UUID, required root/manager offsets, and fingerprints for every called
method and required patch site. Never add a wildcard UUID or a force mode.

The initial profile is `zzz-global-3.1.0-layout2-nullsafe-citygate`. It is tied
to the verified ZZZ 3.1.0 PlayCover binary and its PC-layout, null-safe mouse,
and local `StandaloneInputModule.mousePresent` gate patches.

## Validation boundary

The bridge publishes complete `MOUS` snapshots and one-byte `DLTA` events for
F1-F12 through Unity's Input System. It intentionally does not mutate
`InputDevice` flags: runtime
evidence showed that native-bit promotion did not repair city clicks or DTEXT.
The bridge resolves Unity's existing `Keyboard.current` and queues only USB HID
usages 58 through 69 (F1-F12) to that device. It never adds a second Keyboard,
which would replace `Keyboard.current`. Ordinary keys remain exclusively on the
existing physical UIKit/GameController path to avoid duplicate movement state.
The opt-in `PLAYTOOLS_KEYBOARD_OWNER` build is narrower: it synchronously owns
only A/D/S/W, Q/E, Space, and Left Shift after an `UpdateState` readback. All
menu, interaction, text-input, Option, Command, and lock keys stay on the
original path.
Runtime verification must use one clean process at a time and confirm startup,
camera and keyboard regression safety, and city UI clicks before expanding the
profile registry.
