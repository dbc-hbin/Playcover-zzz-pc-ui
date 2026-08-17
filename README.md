# PlayCover ZZZ PC UI — Stable Serial Keyboard Build

Zenless Zone Zero Global 3.1.0을 PlayCover에서 PC UI와 네이티브 마우스·키보드로 실행하기 위한 재현 가능한 패치 세트입니다. 현재 권위 문서는 [STABLE_BUILD.md](STABLE_BUILD.md)이며, 과거 Deep Research와 `_work/` 자료는 설계 이력일 뿐 배포 기준이 아닙니다.

## 현재 안정 구성

- 게임의 effective UI layout만 PC 값 `2`로 고정합니다. `Application.platform`, Metal, UIKit 경로는 변경하지 않습니다.
- `MouseInputEnhancement`는 활성 상태로 유지하고, 초기 마우스 객체가 없을 때 zero baseline을 제공해 NRE를 방지합니다.
- PlayTools는 Unity InputSystem에 가상 `Mouse` 하나를 게시해 카메라, 절대 위치, 스크롤, 5개 버튼을 전달합니다.
- 키보드는 두 번째 장치를 만들지 않습니다. AppKit 입력을 Unity의 기존 `Keyboard.current`에 1바이트 `DLTA`로 직렬 전달합니다.
- F1–F12 전용 보조 경로는 제거했습니다. F키도 일반 키와 같은 단일 직렬 경로를 사용합니다.

## UnityFramework 패치

대상은 사용자가 보유한 Global 3.1.0 IPA의 `Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework`입니다.

| RVA | 원본 | 변경 | 효과 |
|---|---|---|---|
| `0xEC72310` | `e00313aa` | `40008052` | effective UI layout을 PC `2`로 고정 |
| `0xB392FF0` | `a02a00b4` | `802a00b4` | null mouse state를 zero-baseline 경로로 보냄 |
| `0xB393540` | `12532395` | `00e4002f` | 초기 마우스 baseline을 0으로 설정 |
| `0xB393544` | `11532395` | `aefeff17` | 기존 초기화 흐름으로 복귀 |

`0xB3943B8`은 원본 `74a60139`를 유지하므로 `MouseInputEnhancement`는 활성입니다. city `mousePresent` gate, stream gate, `IsPC`, OSPROD, 공유 platform getter/store, Unity `OnUpdate` hook은 포함하지 않습니다.

```bash
python3 tools/patch_zzz_global_ipa.py input.ipa output.ipa --no-sign
# 또는
python3 tools/patch_zzz_global_ipa.py input.ipa output.ipa \
  --identity "Apple Development: Name (TEAMID)"
```

패처는 네 위치의 원본 바이트를 먼저 확인하고, 크기·실행 권한·symlink·CRC·`SC_Info` 제거·서명을 재검증합니다. 다른 빌드는 fail-closed로 중단합니다.

## 직렬 키보드 정책

직렬화 대상은 Unity mapper가 지원하는 일반 키입니다: A–Z, 숫자, 기호, Enter/Escape/Tab/Backspace/Space, 탐색키, 방향키, 숫자패드, Left Shift, F1–F12.

다음 입력은 기존 AppKit 경로로 통과합니다.

- Option, Command, 좌·우 Control, Right Shift
- Caps/Num/Scroll Lock
- Print Screen, Pause, Context Menu
- 미디어·시스템·지원하지 않는 HID
- 텍스트 필드, 텍스트 뷰, 검색창 편집 중 입력

큐 준비가 실패한 key-down은 원본 AppKit 이벤트를 통과시키며, 그 키의 key-up도 같은 경로를 유지합니다. 앱 비활성화, 키매핑 전환, 텍스트 입력 시작 시 소유 중인 키를 해제합니다.

## 빌드

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
zsh -n tools/*.sh
python3 -m py_compile tools/*.py
zsh tools/build_playtools.sh
zsh tools/build_patched_playcover.sh
```

생성물:

- `dist/PlayTools-SerialKeyboard-nullsafe.framework.zip`
- `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip`
- `dist/ZZZ-PC-UI-SerialKeyboard-SHA256SUMS.txt`

PlayCover 통합 앱은 ad-hoc 서명되며, 내부 `AKInterface.bundle`, `PlayTools.framework`, 최종 `.app`을 `codesign --verify --deep --strict`로 검증합니다.

## 설치와 실행

```bash
stage=$(mktemp -d)
ditto -x -k dist/PlayTools-SerialKeyboard-nullsafe.framework.zip "$stage"
zsh tools/install_playtools_stage_safely.sh \
  "$stage/PlayTools-SerialKeyboard-nullsafe.framework"

zsh tools/launch_zzz_with_patched_playtools.sh
```

런처는 PlayCover 키매핑을 끄고 Unity Native Mouse를 활성화한 뒤, 설치된 PlayTools와 UnityFramework 해시, 코드 서명, 실제 프로세스 매핑을 확인합니다.

## 검증 상태

- PC UI 및 월드 진입: 검증됨
- 카메라, 커서 캡처, Y축, 스크롤·버튼: 검증됨
- 일반 전투 키와 F1–F12 단일 직렬 경로: 현재까지 가장 안정적인 실플레이 구성
- 서명·Mac Catalyst arm64·아카이브 무결성: 빌드마다 검증
- 도시 UI 클릭과 게임 내부 리바인딩 표시는 별도 게임 경계이며 이 패치가 보장하지 않습니다.

IPAs, DMGs, 계정 정보, 서명 인증서는 저장소에 커밋하지 않습니다.
