# ZZZ PC UI Stable Build Specification

상태: **현재 배포 기준**
대상: ZZZ Global 3.1.0 / PlayCover 3.1.0 / arm64 Mac Catalyst

## 1. 권위 있는 구성

현재 기준은 PC layout 강제와 MouseInputEnhancement null-safe 초기화, PlayTools 네이티브 Mouse, 기존 Unity Keyboard를 이용한 직렬 키보드 생산자로 구성됩니다.

검증된 UnityFramework 설치본:

```text
SHA-256 a2a91fa284bb126f3bfb7c72f311c1a34bd18afe67daaad658c7bca5358c8f2f
size    477,908,736 bytes
```

UnityFramework 현재 바이트:

```text
0x0EC72310  40008052  PC effective layout = 2
0x0B392FF0  802a00b4  null state branch
0x0B393540  00e4002f  zero baseline
0x0B393544  aefeff17  initialization rejoin
0x0B3943B8  74a60139  MouseInputEnhancement enabled, unchanged
0x13685CD8  60000034  city mousePresent gate, unchanged
0x13686110  c0000036  stream gate, unchanged
0x130D6D24  eb2bb86d  InputManager.OnUpdate entry, unchanged
```

## 2. PlayTools 입력 경계

PlayTools는 UUID, bundle identifier, ABI version, code fingerprint가 모두 일치할 때만 Unity 함수를 호출합니다. 일치하지 않으면 프로세스에서 영구 비활성화합니다.

Mouse 경로:

```text
AKInterface AppKit monitor
  -> PlayTools MouseState snapshot
  -> Unity InputSystem QueueEvent
  -> one virtual Mouse
```

Keyboard 경로:

```text
AppKit keyDown/keyUp
  -> USB HID to Unity Key mapping
  -> existing Keyboard.current byte RMW
  -> one-byte DeltaStateEvent
  -> consume AppKit edge only after queue preconditions pass
```

키보드에는 새 장치를 추가하지 않습니다. F1–F12도 별도 보조 이벤트 없이 같은 경로를 사용합니다. repeat는 해당 key-down을 직렬 경로가 소유한 동안만 소비합니다.

통과 정책은 다음과 같습니다.

```text
Option, Command, Left/Right Control, Right Shift
CapsLock, NumLock, ScrollLock
PrintScreen, Pause, ContextMenu
media/system/unknown HID
all keys while a text editor is active
```

## 3. 포함하지 않는 실험

- synchronous KeyboardOwner/UpdateState
- release correction and delayed release
- GCKeyboard stability gate
- InputArbiter ring/generation/watchdog
- InvokeAfterUpdate or OnUpdate inline hook
- UnityFramework code cave/remap
- second virtual Keyboard
- city mousePresent/stream bypass

이 코드 일부가 compile gate 아래 남아 있어도 안정 빌드는 관련 매크로를 정의하지 않으며, 빌드 스크립트가 arbiter/hook 심볼을 거부합니다.

## 4. 배포 세트

```text
dist/PlayTools-SerialKeyboard-nullsafe.framework.zip
dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip
tools/patch_zzz_global_ipa.py
dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt
```

PlayCover 앱은 PlayTools 프레임워크를 포함하지만 게임의 UnityFramework는 포함하지 않습니다. 사용자가 보유한 IPA는 패처로 별도 처리합니다.

## 5. 검증 요구사항

1. Python 회귀 테스트, zsh syntax, Python compile을 통과해야 합니다.
2. PlayTools는 arm64 `MACCATALYST`여야 합니다.
3. `AKInterface.bundle`, `PlayTools.framework`, PlayCover `.app`의 deep/strict 코드 서명이 유효해야 합니다.
4. ZIP을 다시 풀어 내부 바이너리 SHA-256과 서명을 재검증해야 합니다.
5. 런타임 주장은 실제 ZZZ 프로세스가 `~/Library/Frameworks/PlayTools.framework`를 매핑한 경우에만 합니다.
