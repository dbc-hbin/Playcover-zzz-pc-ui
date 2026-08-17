# PlayCover ZZZ PC UI

[English](README.md) | [한국어](README.ko.md)

A patch set for running Zenless Zone Zero Global 3.1.0 on an Apple Silicon Mac with the **PC UI, native mouse controls, and serial keyboard input**.

> **Press Option to switch between camera control and the visible cursor.**

> **After patching, never change the in-game input mode back to Touchscreen. Changing it back in Settings does not reliably restore the patched input path; you must reinstall the patched IPA.**

The first half of this README is a user-friendly installation guide. Exact bytes, hashes, and maintenance boundaries are in [Agent and Contributor Technical Reference](#agent-and-contributor-technical-reference). [STABLE_BUILD.md](STABLE_BUILD.md) is the authoritative release specification.

> This repository does not distribute the game IPA. You must provide your own **Global 3.1.0 IPA**. The patcher fails closed when the original bytes do not match.

## What changes?

- The game uses its PC UI layout instead of the mobile layout.
- The native mouse controls the camera, visible cursor, wheel, and five buttons.
- **Option switches between camera control and the visible cursor.**
- Keyboard events are delivered in order to Unity's one existing keyboard device.
- F1–F12 use the same serial path as ordinary keys.
- Option, Command, Control, Right Shift, lock keys, and system keys stay with macOS.

## Requirements

- Apple Silicon Mac
- A user-owned ZZZ Global 3.1.0 IPA
- These stable distribution files:
  - `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip`
  - `dist/PlayTools-SerialKeyboard-nullsafe.framework.zip`
- Xcode Command Line Tools and Python 3 if you want to patch or rebuild locally

## Quick start

### 1. Install PlayCover ZZZ PC UI

In Finder, extract `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip` and move **Playcover ZZZ PC UI.app** to `/Applications`.

To extract it in Terminal and open the temporary folder in Finder:

```bash
stage=$(mktemp -d)
ditto -x -k dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip "$stage"
open "$stage"
```

Quit the existing app before replacing it, and keep a backup if needed. This distribution uses an ad-hoc signature for local use.

### 2. Patch your IPA

Run the patcher from the repository root. If the output path is omitted, it creates `<input-name>-pcui.ipa`.

```bash
python3 tools/patch_zzz_global_ipa.py \
  "$HOME/Downloads/ZenlessZoneZero-3.1.0.ipa"
```

The default is an ad-hoc signature. To use an Apple Development identity:

```bash
python3 tools/patch_zzz_global_ipa.py input.ipa output-pcui.ipa \
  --identity "Apple Development: Name (TEAMID)"
```

`--no-sign` is for patch inspection and tests only. Do not use an unsigned IPA as the normal installation artifact.

### 3. Install the patched IPA

1. Open `/Applications/Playcover ZZZ PC UI.app`.
2. Install the `*-pcui.ipa` produced in step 2.
3. Disable **PlayCover key mapping** for the game.
4. Enable **Experimental Unity Native Mouse**.
5. Do not change the in-game input mode to **Touchscreen** after installation. If you do, reinstall the patched IPA instead of only changing the setting back.
6. Quit both PlayCover and the game before installing PlayTools.

The PlayCover app archive embeds PlayTools. Installing the verified user framework below is still recommended because that is the copy loaded directly by the game.

### 4. Install the stable PlayTools framework

```bash
stage=$(mktemp -d)
ditto -x -k dist/PlayTools-SerialKeyboard-nullsafe.framework.zip "$stage"
zsh tools/install_playtools_stage_safely.sh \
  "$stage/PlayTools-SerialKeyboard-nullsafe.framework"
```

The installer:

- refuses to run while PlayCover or ZZZ is running;
- backs up the existing `~/Library/Frameworks/PlayTools.framework`;
- validates the Mac Catalyst platform and code signature;
- verifies the installed SHA-256; and
- attempts rollback if installation fails.

### 5. Launch

For normal use, open **Playcover ZZZ PC UI.app** and launch the installed game.

On the already-prepared development machine, this helper performs additional fixed-path and hash checks before launching:

```bash
zsh tools/launch_zzz_with_patched_playtools.sh
```

The launcher expects the repository's known game path and exact UnityFramework hash. It is not a general first-time installer for a new Mac.

## Check that it works

- Enter the world and confirm that the PC UI is visible.
- Move the camera and cursor, then test clicks and the mouse wheel.
- Press Option and confirm that it switches between camera control and the visible cursor.
- Press movement and combat keys together in quick succession.
- Test F1–F12.
- Open a text field and confirm that typing is not consumed as game input.

This serial keyboard build is the most stable configuration tested so far, but it does not replace every part of the game's own input handling. City UI clicking and the game's internal rebinding display remain separate game boundaries.

## Troubleshooting

### `original bytes mismatch` or unsupported build

The IPA is not Global 3.1.0, or its UnityFramework already contains another patch. Do not force the patch; start again from the correct original IPA.

### The PlayTools installer detects a running process

Quit PlayCover and ZZZ completely, then retry. The installer intentionally refuses to replace a loaded framework.

### Keys are duplicated or routed incorrectly

Disable PlayCover's per-game key mapping and verify that Experimental Unity Native Mouse is enabled. Text editors intentionally use the original AppKit path.

### Input mode was changed to Touchscreen

Do not keep toggling the setting. Reinstall the patched `*-pcui.ipa`; changing the input mode back in Settings is not a reliable recovery path.

### macOS blocks the app

This is an ad-hoc-signed local build. After verifying that the archive came from this repository, use **Control-click → Open** in Finder. Do not disable system-wide security or organizational Gatekeeper policy.

## Rebuild locally

```bash
zsh tools/build_playtools.sh
zsh tools/build_patched_playcover.sh
```

The stable release consists of these files. Artifacts with Arbiter, Owner, or ReleaseCorrection in their names are historical experiments and are not part of the current distribution.

```text
dist/PlayTools-SerialKeyboard-nullsafe.framework.zip
dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip
dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt
```

Print the current checksums with:

```bash
shasum -a 256 \
  dist/PlayTools-SerialKeyboard-nullsafe.framework.zip \
  dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip \
  tools/patch_zzz_global_ipa.py
```

Compare the results with `dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt`.

---

# Agent and Contributor Technical Reference

## Authority and supported scope

Use this authority order:

1. [STABLE_BUILD.md](STABLE_BUILD.md)
2. The stable-build description and patch table in this README
3. [GOAL_ZZZ_UI_LAYOUT.md](GOAL_ZZZ_UI_LAYOUT.md)
4. Current build and patch scripts, followed by regression tests

[EXPERIMENTS.md](EXPERIMENTS.md) summarizes the discarded approaches and their conclusions. InputArbiter/KeyboardOwner/ReleaseCorrection artifacts are not part of the stable distribution.

Supported target: ZZZ Global 3.1.0, PlayCover 3.1.0, arm64 Mac Catalyst. Never commit IPAs, DMGs, credentials, signing identities, or container data.

## Stable input architecture

```text
AppKit keyDown/keyUp
  -> USB HID to Unity Key mapping
  -> existing Keyboard.current byte RMW
  -> one-byte DeltaStateEvent
  -> consume the AppKit edge only after queue preconditions succeed
```

- Do not add a second Keyboard.
- There is no supplemental F1–F12 path.
- If queueing a press fails, keep the entire key cycle on the AppKit passthrough path.
- Release serial ownership when the app deactivates, key mapping changes, or text input begins.
- The stable build excludes synchronous `UpdateState`, release correction, delayed release, GCKeyboard gates, arbiter rings, generation/watchdogs, and `OnUpdate`/`InvokeAfterUpdate` hooks.

The serialized mapper-supported range includes letters, digits, punctuation, editing keys, Space, navigation and arrow keys, the numeric keypad, Left Shift, and F1–F12. These inputs pass through:

```text
Option, Command, Left/Right Control, Right Shift
CapsLock, NumLock, ScrollLock
PrintScreen, Pause, ContextMenu
media/system/unknown HID
all keys while a text editor is active
```

Option remains outside serialization because it switches between camera control and the visible cursor.

## Exact UnityFramework patch

The patch target is `Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework`.

| RVA | Original | Replacement | Purpose |
|---|---|---|---|
| `0x0EC72310` | `e00313aa` | `40008052` | Force effective UI layout to PC value `2` |
| `0x0B392FF0` | `a02a00b4` | `802a00b4` | Route a null mouse state to the zero-baseline path |
| `0x0B393540` | `12532395` | `00e4002f` | Set the initial mouse baseline to zero |
| `0x0B393544` | `11532395` | `aefeff17` | Rejoin the original initialization flow |

These boundaries remain unchanged:

```text
0x0B3943B8  74a60139  MouseInputEnhancement enabled
0x13685CD8  60000034  city mousePresent gate
0x13686110  c0000036  stream gate
0x130D6D24  eb2bb86d  InputManager.OnUpdate entry
```

Do not patch `Application.platform`, `IsPC`, OSPROD, shared platform getters/stores, city/stream gates, or a Unity code cave. The patcher validates all four original byte sequences before writing, then rechecks file size, executable permissions, symlinks, archive CRC, and signatures.

Verified installed UnityFramework:

```text
SHA-256  a2a91fa284bb126f3bfb7c72f311c1a34bd18afe67daaad658c7bca5358c8f2f
size     477,908,736 bytes
```

## Stable artifact identities

```text
PlayTools ZIP   7ee375ddc1abc21a996251edcf74485cd4595358a273b72b70f12ae44b083df7
PlayCover ZIP   5c10c29725b695fe2692629330306f43bad775fea612203b34ef8fa0cbdc44b4
PlayTools Mach-O
                132701254ba9a4314e53476f702917f28f9dee2928fc427f8c03d16d4a41db96
AKInterface     dab26672197b44e99b7fa9b03f2cad5b73c5fe69cf0ae9ca58657a0c7553765c
```

The PlayCover app archive embeds PlayTools but does not contain the game's UnityFramework. Patch the user-owned IPA separately.

## Build and regression checks

```bash
python3 -m unittest \
  tests.test_patch_zzz_global_ipa \
  tests.test_playtools_function_key_scope \
  tests.test_playtools_serial_keyboard -v

zsh -n \
  tools/build_playtools.sh \
  tools/build_playtools_serial_keyboard.sh \
  tools/build_patched_playcover.sh \
  tools/launch_zzz_with_patched_playtools.sh

python3 -m py_compile tools/patch_zzz_global_ipa.py
zsh tools/build_playtools.sh
zsh tools/build_patched_playcover.sh
```

`tools/build_playtools.sh` is a thin entry point for the stable `build_playtools_serial_keyboard.sh`. Do not reconnect experimental build scripts to the default build or documentation.

## Release verification boundary

Before publishing a release:

1. Verify that PlayTools is arm64 `MACCATALYST`.
2. Extract the archives again and verify the PlayTools and AKInterface SHA-256 values.
3. Run `codesign --verify --deep --strict` on `AKInterface.bundle`, `PlayTools.framework`, and the final PlayCover `.app`.
4. Run `unzip -tq` on both ZIP archives.
5. Make runtime claims only after the ZZZ process maps the verified `~/Library/Frameworks/PlayTools.framework`.

Static bytes, a successful build, and a valid signature do not by themselves prove runtime playability. Record runtime results separately.
