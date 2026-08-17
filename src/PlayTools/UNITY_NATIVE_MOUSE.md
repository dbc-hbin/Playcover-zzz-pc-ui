# Profile-Gated Unity Native Input

PlayTools publishes native InputSystem events only for the reviewed ZZZ Global 3.1.0 UnityFramework. The feature is controlled by **Input Compatibility → Unity Native Mouse (Experimental)** and requires PlayCover keymapping to be disabled.

## Fail-Closed Profile

Initialization requires all of the following:

- exact application bundle identifier;
- exact UnityFramework Mach-O UUID;
- supported profile/event encoder versions;
- readable IL2CPP root and InputSystem manager;
- all reviewed 16-byte code fingerprints.

Runtime-not-ready states are retried at 250 ms intervals. UUID, schema, or fingerprint mismatches permanently disable native input for that process before game code is called.

The stable profile targets the PC-layout, enabled-MouseInputEnhancement, null-safe UnityFramework described in the repository `STABLE_BUILD.md`. The discarded city and stream gates are not part of the profile.

## Mouse

PlayTools creates one Unity `Mouse`, verifies that it becomes the current mouse, and queues complete `MOUS` state snapshots containing:

- absolute position with aspect-fit/letterbox conversion;
- relative camera delta with corrected Y direction;
- horizontal and vertical scroll;
- left, right, middle, forward, and back buttons.

Motion is limited to roughly 125 Hz. Relative movement is sent only while the cursor is captured; absolute position remains current in UI mode.

## Serialized Keyboard

PlayTools never creates another Keyboard. It resolves Unity's existing `Keyboard.current`, tracks AppKit key edges, and queues a one-byte `DLTA` for the affected key-state byte.

All mapper-supported ordinary keys and F1–F12 use this single path. The AppKit edge is consumed only after the queue preconditions succeed. If a key-down cannot be queued, that full key cycle remains on the original AppKit path. Repeats are consumed only for a serial-owned key.

The following remain passthrough:

- Option, Command, Left/Right Control, Right Shift;
- CapsLock, NumLock, ScrollLock;
- PrintScreen, Pause, ContextMenu;
- media, system, and unknown HID usages;
- every key while a text editor is active.

Focus loss, PlayCover keymapping activation, text editing, and input reset release every serial-owned key.

## Explicit Non-Features

The stable build does not define or install KeyboardOwner, release correction, delayed-release masks, GCKeyboard gates, InputArbiter, inline hooks, remap payloads, or Unity `OnUpdate` patches. Build validation rejects the arbiter and after-update hook symbols.
