# PlayCover ZZZ PC UI

[English](README.md) | [한국어](README.ko.md)

Zenless Zone Zero Global 3.1.0을 Apple Silicon Mac에서 **PC UI, 네이티브 마우스, 직렬 키보드 입력**으로 실행하기 위한 패치 세트입니다.

> **Option 키를 눌러 카메라 조작과 보이는 커서를 전환합니다.**

> **패치 후 게임 입력 방식을 절대 `화면 터치`로 바꾸지 마세요. 설정에서 다시 되돌리는 것만으로는 패치 입력 경로가 정상 복구되지 않으며, 패치된 IPA를 다시 설치해야 합니다.**

이 README의 위쪽은 일반 사용자를 위한 설치 안내이고, 아래쪽의 [에이전트·기여자 기술 참조](#에이전트기여자-기술-참조)는 정확한 바이트와 빌드 경계를 다룹니다. 현재 배포 규격의 최종 기준은 [STABLE_BUILD.md](STABLE_BUILD.md)입니다.

> 이 저장소는 게임 IPA를 제공하지 않습니다. 본인이 소유한 **Global 3.1.0 IPA**가 필요합니다. 다른 버전은 패처가 원본 바이트 불일치로 중단합니다.

## 무엇이 달라지나요?

- 게임 UI가 모바일 레이아웃 대신 PC 레이아웃을 사용합니다.
- 마우스로 카메라 회전, 커서 이동, 스크롤과 5개 버튼을 사용할 수 있습니다.
- Option 키를 눌러 카메라 조작과 보이는 커서를 전환합니다.
- 키보드 입력을 Unity의 기존 키보드 장치 한 개로 순서대로 전달합니다.
- F1–F12도 일반 키와 같은 경로를 사용합니다.
- Option, Command, Control, Right Shift와 잠금·시스템 키는 macOS에 그대로 맡깁니다.

## 준비물

- Apple Silicon Mac
- ZZZ Global 3.1.0 IPA
- 이 저장소의 안정 배포 파일:
  - `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip`
  - `dist/PlayTools-SerialKeyboard-nullsafe.framework.zip`
- 패치를 직접 실행하거나 다시 빌드하려면 Xcode Command Line Tools와 Python 3

## 빠른 시작

### 1. PlayCover ZZZ PC UI 설치

Finder에서 `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip`을 풀고 생성된 **Playcover ZZZ PC UI.app**을 `/Applications`로 옮깁니다.

터미널에서 압축만 풀고 Finder로 열려면:

```bash
stage=$(mktemp -d)
ditto -x -k dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip "$stage"
open "$stage"
```

기존 앱이 있다면 먼저 종료하고 기존 앱을 백업하거나 교체하세요. 이 앱은 로컬 사용을 위한 ad-hoc 서명 빌드입니다.

### 2. 본인 소유 IPA 패치

저장소 루트에서 실행합니다. 출력 파일명을 생략하면 `<입력파일명>-pcui.ipa`가 생성됩니다.

```bash
python3 tools/patch_zzz_global_ipa.py \
  "$HOME/Downloads/ZenlessZoneZero-3.1.0.ipa"
```

기본값은 ad-hoc 서명입니다. Apple Development 인증서로 서명하려면:

```bash
python3 tools/patch_zzz_global_ipa.py input.ipa output-pcui.ipa \
  --identity "Apple Development: Name (TEAMID)"
```

`--no-sign`은 바이트 패치만 확인하는 개발·테스트 용도입니다. 일반 설치본에는 사용하지 마세요.

### 3. 패치된 IPA 설치

1. `/Applications/Playcover ZZZ PC UI.app`을 엽니다.
2. 2단계에서 만든 `*-pcui.ipa`를 PlayCover에 설치합니다.
3. 게임 설정에서 **PlayCover 키매핑을 끄고**, **Experimental Unity Native Mouse를 켭니다**.
4. 설치 후 게임 입력 방식을 **화면 터치**로 바꾸지 마세요. 바꿨다면 설정만 되돌리지 말고 패치된 IPA를 다시 설치하세요.
5. PlayCover와 게임을 모두 종료합니다.

PlayCover 앱 ZIP에는 PlayTools가 포함되어 있지만, 게임이 실제로 읽는 사용자 프레임워크를 확실히 맞추려면 다음 단계도 실행하는 것을 권장합니다.

### 4. 안정 PlayTools 설치

```bash
stage=$(mktemp -d)
ditto -x -k dist/PlayTools-SerialKeyboard-nullsafe.framework.zip "$stage"
zsh tools/install_playtools_stage_safely.sh \
  "$stage/PlayTools-SerialKeyboard-nullsafe.framework"
```

설치기는 다음 작업을 자동으로 수행합니다.

- PlayCover나 ZZZ가 실행 중이면 설치 거부
- 기존 `~/Library/Frameworks/PlayTools.framework` 백업
- Mac Catalyst 형식과 코드 서명 확인
- 복사 후 SHA-256 재확인
- 실패 시 이전 프레임워크로 복구 시도

### 5. 실행

일반 사용자는 **Playcover ZZZ PC UI.app**에서 설치한 게임을 실행하면 됩니다.

이 저장소에서 이미 준비된 정확한 ZZZ 설치본을 검증하며 실행하는 개발 환경에서는 다음 헬퍼를 사용할 수 있습니다.

```bash
zsh tools/launch_zzz_with_patched_playtools.sh
```

이 런처는 고정된 앱 경로와 UnityFramework 해시를 요구합니다. 새 컴퓨터에서 처음 설치할 때 쓰는 범용 설치기는 아닙니다.

## 정상 동작 확인

- 월드 진입 후 UI가 PC 레이아웃으로 표시되는지 확인합니다.
- 마우스로 카메라와 커서를 움직이고 스크롤·클릭을 확인합니다.
- Option 키를 눌러 카메라 조작과 보이는 커서가 전환되는지 확인합니다.
- 이동키와 전투키를 함께 빠르게 눌러 봅니다.
- F1–F12를 확인합니다.
- 텍스트 입력창에서는 키가 게임 입력으로 가로채지지 않는지 확인합니다.

현재 직렬 키보드 빌드는 지금까지 시험한 구성 중 가장 안정적이지만, 게임 자체의 입력 처리까지 완전히 대체하지는 않습니다. 도시 UI 클릭과 게임 내부 리바인딩 표시는 별도 게임 경계입니다.

## 자주 생기는 문제

### `original bytes mismatch` 또는 지원하지 않는 빌드

Global 3.1.0과 다른 IPA이거나 이미 다른 패치가 적용된 UnityFramework입니다. 강제로 진행하지 말고 원본 IPA를 다시 확인하세요.

### PlayTools 설치기가 실행 중인 프로세스를 감지함

PlayCover와 ZZZ를 완전히 종료한 뒤 다시 실행하세요. 실행 중인 프레임워크는 교체하지 않습니다.

### 키가 이중 입력되거나 입력 방식이 이상함

PlayCover의 게임별 키매핑을 끄고 Experimental Unity Native Mouse가 켜져 있는지 확인하세요. 텍스트 필드에서는 의도적으로 원래 AppKit 입력을 사용합니다.

### 입력 방식을 화면 터치로 바꿈

설정을 반복해서 전환하지 마세요. `*-pcui.ipa`를 다시 설치해야 하며, 설정에서 입력 방식만 되돌리는 것은 신뢰할 수 있는 복구 방법이 아닙니다.

### macOS가 앱 실행을 막음

이 배포본은 ad-hoc 서명입니다. 본인이 이 저장소에서 받은 파일임을 확인한 뒤 Finder의 **우클릭 → 열기**를 사용하세요. 조직 정책이나 Gatekeeper를 우회하도록 시스템 보안을 전역으로 끄지는 마세요.

## 직접 다시 빌드하기

```bash
zsh tools/build_playtools.sh
zsh tools/build_patched_playcover.sh
```

안정 산출물은 다음 세 파일입니다. `dist/`의 Arbiter, Owner, ReleaseCorrection 이름이 붙은 파일은 과거 실험용이며 현재 배포 기준이 아닙니다.

```text
dist/PlayTools-SerialKeyboard-nullsafe.framework.zip
dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip
dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt
```

현재 체크섬은 다음 명령으로 표시할 수 있습니다.

```bash
shasum -a 256 \
  dist/PlayTools-SerialKeyboard-nullsafe.framework.zip \
  dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip \
  tools/patch_zzz_global_ipa.py
```

기대값은 `dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt`와 비교하세요.

---

# 에이전트·기여자 기술 참조

## 권위와 지원 범위

권위 순서는 다음과 같습니다.

1. [STABLE_BUILD.md](STABLE_BUILD.md)
2. 이 README의 안정 빌드 설명과 패치 표
3. [GOAL_ZZZ_UI_LAYOUT.md](GOAL_ZZZ_UI_LAYOUT.md)
4. 현재 빌드·패치 스크립트와 회귀 테스트

[EXPERIMENTS.md](EXPERIMENTS.md)에 폐기한 접근과 결론을 간략히 정리했습니다. InputArbiter/KeyboardOwner/ReleaseCorrection 산출물은 안정 배포에 포함하지 않습니다.

지원 대상은 ZZZ Global 3.1.0, PlayCover 3.1.0, arm64 Mac Catalyst다. IPA, DMG, 인증서, 계정 정보, 컨테이너 데이터는 커밋하지 않는다.

## 안정 입력 구조

```text
AppKit keyDown/keyUp
  -> USB HID to Unity Key mapping
  -> existing Keyboard.current byte RMW
  -> one-byte DeltaStateEvent
  -> queue preconditions 성공 시에만 AppKit edge 소비
```

- 두 번째 Keyboard를 추가하지 않는다.
- F1–F12 전용 supplemental 경로가 없다.
- press 큐잉 실패 시 해당 키 cycle 전체를 AppKit passthrough로 유지한다.
- 앱 비활성화, 키매핑 전환, 텍스트 입력 시작 시 직렬 소유 상태를 해제한다.
- synchronous `UpdateState`, release correction, delayed release, GCKeyboard gate, arbiter ring, generation/watchdog, `OnUpdate`/`InvokeAfterUpdate` hook을 안정 빌드에 포함하지 않는다.

직렬화하는 mapper-supported 범위는 문자, 숫자, 기호, 편집키, Space, 탐색·방향키, 숫자패드, Left Shift, F1–F12다. 다음은 passthrough다.

```text
Option, Command, Left/Right Control, Right Shift
CapsLock, NumLock, ScrollLock
PrintScreen, Pause, ContextMenu
media/system/unknown HID
all keys while a text editor is active
```

## UnityFramework 정확한 패치

패처 대상은 `Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework`이다.

| RVA | 원본 | 변경 | 목적 |
|---|---|---|---|
| `0x0EC72310` | `e00313aa` | `40008052` | effective UI layout을 PC `2`로 고정 |
| `0x0B392FF0` | `a02a00b4` | `802a00b4` | null mouse state를 zero-baseline 경로로 분기 |
| `0x0B393540` | `12532395` | `00e4002f` | 초기 mouse baseline을 0으로 설정 |
| `0x0B393544` | `11532395` | `aefeff17` | 기존 초기화 흐름으로 복귀 |

다음 경계는 변경하지 않는다.

```text
0x0B3943B8  74a60139  MouseInputEnhancement enabled
0x13685CD8  60000034  city mousePresent gate
0x13686110  c0000036  stream gate
0x130D6D24  eb2bb86d  InputManager.OnUpdate entry
```

`Application.platform`, `IsPC`, OSPROD, 공유 platform getter/store, city/stream gate, Unity code cave를 건드리지 않는다. 패처는 네 원본 바이트를 전부 사전 검증한 뒤 쓰고, 크기·실행 권한·symlink·CRC·서명 결과를 다시 확인한다.

검증된 UnityFramework 설치본:

```text
SHA-256  a2a91fa284bb126f3bfb7c72f311c1a34bd18afe67daaad658c7bca5358c8f2f
size     477,908,736 bytes
```

## 안정 산출물 식별자

```text
PlayTools ZIP   7ee375ddc1abc21a996251edcf74485cd4595358a273b72b70f12ae44b083df7
PlayCover ZIP   5c10c29725b695fe2692629330306f43bad775fea612203b34ef8fa0cbdc44b4
PlayTools Mach-O
                132701254ba9a4314e53476f702917f28f9dee2928fc427f8c03d16d4a41db96
AKInterface     dab26672197b44e99b7fa9b03f2cad5b73c5fe69cf0ae9ca58657a0c7553765c
```

PlayCover 앱 ZIP은 PlayTools를 포함하지만 게임의 UnityFramework는 포함하지 않는다. UnityFramework는 사용자가 소유한 IPA에 별도로 패치한다.

## 빌드와 회귀 검사

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

`tools/build_playtools.sh`는 안정 `build_playtools_serial_keyboard.sh`의 얇은 진입점이다. 실험 빌드 스크립트를 기본 빌드나 문서에 다시 연결하지 않는다.

## 배포 검증 경계

릴리스 전에 다음을 확인한다.

1. PlayTools가 arm64 `MACCATALYST`인지 확인한다.
2. 압축을 다시 풀어 PlayTools와 AKInterface SHA-256을 확인한다.
3. `AKInterface.bundle`, `PlayTools.framework`, 최종 PlayCover `.app`에 `codesign --verify --deep --strict`를 실행한다.
4. 두 ZIP에 `unzip -tq`를 실행한다.
5. 실제 실행 주장은 ZZZ 프로세스가 검증된 `~/Library/Frameworks/PlayTools.framework`를 매핑한 경우에만 한다.

정적 바이트, 빌드 성공, 서명 성공은 런타임 플레이 성공과 같은 의미가 아니다. 런타임 결과는 별도로 기록한다.
