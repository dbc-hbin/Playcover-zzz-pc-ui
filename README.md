# Playcover ZZZ PC UI — Global 3.1.0 Patch Set / ZZZ 글로벌 3.1.0 통합 패치 세트

> **Authority / 권위 문서**: `GOAL_ZZZ_UI_LAYOUT.md` + `_work/static-wide/PC_UI_MOUSE_BRIDGE_CONSOLIDATED_20260813_V2.md`  
> Validated on live device: PC login/title UI + world entry (`CloseLogin → SceneSwitch complete`), no `MouseInputEnhancement` NRE.

---

## English

### What this repo contains

| Path | Description |
|---|---|
| `tools/patch_zzz_global_ipa.py` | **Minimal working patch** — applies exactly 2 verified UnityFramework patches to a user-owned IPA and re-signs it |
| `src/PlayTools` | Patched PlayTools source for Option camera mode, F1–F4 forwarding, and corrected Y-axis input |
| `dist/PlayTools-camera-yfix-nullsafe-nocity.framework.zip` | Verified patched PlayTools framework |
| `patches/playcover-3.1.0-combined.patch` | Reproducible PlayCover 3.1.0 source patch for the combined app |
| `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip` | Combined PlayCover + patched PlayTools build |

Patched IPAs (`*.ipa`, `*.dmg`) are never committed.

### The 2-site patch (and only this)

Both at `Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework`:

| RVA | Original | Patched | Effect |
|---|---|---|---|
| `0xEC72310` | `e00313aa` `mov x0, x19` | `40008052` `mov w0, #2` | Force game-owned effective UI layout to **PC (2)**. `Application.platform` stays `4`, Metal/Gfx untouched |
| `0xB3943B8` | `74a60139` `strb w20,[x19,#0x69]` | `7fa60139` `strb wzr,[x19,#0x69]` | Force `MouseInputEnhancement` disabled before `InitAllState` — avoids NRE when iOS mouse object at `+0x160` is null |

*Size preserved, 5 bytes changed. Aborts if original bytes don't match (wrong build).*

**Explicitly NOT included** (discarded per V2 §6): city gate `0x13685CD8`, stream gate `0x13686110`, `IsPCPlatform` / IL2CPP `IsPC`, `SwitchUILayoutPlatform` / `ConfirmUILayout`, `OSPROD`, shared `0x16584/0x16808/0x16850`.

### Requirements

- macOS with Xcode CLT (`codesign`, `xcrun vtool`, `otool`)
- Python 3.8+
- A **user-owned** `com.HoYoverse.Nap_3.1.0_und3fined.ipa` (not distributed)

### Usage — IPA patch

```bash
# ad-hoc (TrollStore / Sideloadly)
python3 tools/patch_zzz_global_ipa.py com.HoYoverse.Nap_3.1.0_und3fined.ipa
# → com.HoYoverse.Nap_3.1.0_und3fined-pcui.ipa (SC_Info stripped, ad-hoc signed, --verify passed)

# Apple Developer (personal signing)
python3 tools/patch_zzz_global_ipa.py input.ipa output.ipa --identity "Apple Development: Your Name (TEAMID)"

# patch only, no signing (test)
python3 tools/patch_zzz_global_ipa.py input.ipa --no-sign
```

The script: path-safe, metadata-preserving `ditto` extract → verify bytes at
both offsets → patch → enforce executable mode `0755` and preserve entitlements → remove `SC_Info`
while retaining untouched nested signatures → `codesign --force --sign <id>
--timestamp=none` (framework → app) → `codesign --verify --deep --strict` →
atomic `ditto` repack → verify CRC, patch bytes, executable mode, symlinks, and
`SC_Info` removal inside the IPA. It refuses to overwrite the source IPA.

### Usage — PlayTools (optional, macOS PlayCover)

Fixes 3.1.0 camera + F1–F4 via profile-gated virtual Mouse. Installed only if you use PlayCover on macOS.

```bash
stage=$(mktemp -d)
ditto -x -k dist/PlayTools-camera-yfix-nullsafe-nocity.framework.zip "$stage"
zsh tools/install_playtools_stage_safely.sh "$stage/PlayTools-camera-yfix.framework"
# installs only ~/Library/Frameworks, preserves PlayCover's Developer ID signature,
# verifies hashes, and backs up to _work/install-backups/
```

Then in PlayCover: **Input Compatibility → Unity Native Mouse (Experimental)** → restart.

For macOS 27, build the combined PlayCover 3.1.0 + patched PlayTools app. The
source patch launches the canonical managed app URL instead of PlayCover's
symlink bundle, preventing the misleading `(null)` LaunchServices permission
alert. It also embeds the verified Option/F1–F4/Y-fix PlayTools build:

```bash
zsh tools/build_patched_playcover.sh
# → dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip
```

The combined app is ad-hoc signed because no local Developer ID identity is
available. It disables KeyCover in this local build so the old Developer ID
keychain ACL cannot block startup; the existing plaintext PlayChain database is
preserved. Install it as `/Applications/Playcover ZZZ PC UI.app`; do not keep a
second `/Applications/PlayCover.app`. The normal in-app ZZZ launch button is the
verified launch path.

The wrapper selects `PlayTools-camera-yfix-nullsafe-nocity.framework.zip`. This
is the verified Y-fix binary paired with the layout-2 + MouseInputEnhancement
null-safe UnityFramework. Only the embedded city-gate compatibility fingerprint
is retargeted; the discarded city-gate game patch is not installed.

### Install from Release

Download from [Releases](../../releases):

- `PlayTools-camera-yfix-nullsafe-nocity.framework.zip`
- `Playcover-ZZZ-PC-UI-3.1.0.app.zip`
- `patch_zzz_global_ipa.py` (or clone this repo)

### Status

- PC UI + world entry: **verified on device**
- Camera + F1–F4 via PlayTools: **verified**
- City/free-roam cursor click: **known failure** (C/F gates did not fix; next candidate is description-based `AddDevice` ABI-v3, not yet shipped)
- Settings `DTEXT` / rebind: **open** (see V2)

---

## 한국어

### 이 저장소가 포함하는 것

| 경로 | 설명 |
|---|---|
| `tools/patch_zzz_global_ipa.py` | **작동하는 최소 패치** — 사용자가 보유한 IPA에 검증된 2곳만 패치하고 자동 재서명 |
| `src/PlayTools` | Option 카메라 모드, F1–F4 전달, Y축 보정을 포함한 PlayTools 소스 |
| `dist/PlayTools-camera-yfix-nullsafe-nocity.framework.zip` | 검증된 패치 PlayTools 프레임워크 |
| `patches/playcover-3.1.0-combined.patch` | 통합 앱을 재현하는 PlayCover 3.1.0 소스 패치 |
| `dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip` | PlayCover + 패치된 PlayTools 통합 빌드 |

패치된 IPA(`*.ipa`, `*.dmg`)는 절대 커밋하지 않음.

### 2곳 패치 (이것만)

대상: `Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework`

| RVA | 원본 | 패치 | 효과 |
|---|---|---|---|
| `0xEC72310` | `e00313aa` `mov x0, x19` | `40008052` `mov w0, #2` | 게임 전용 effective UI layout을 **PC(2)** 로 고정. `Application.platform`은 `4` 유지, Metal/Gfx 미변경 |
| `0xB3943B8` | `74a60139` `strb w20,[x19,#0x69]` | `7fa60139` `strb wzr,[x19,#0x69]` | `MouseInputEnhancement`를 `InitAllState` 전에 disabled로 강제 — iOS 마우스 객체(`+0x160`)가 null일 때 NRE 방지 |

*파일 크기 유지, 5바이트 변경. 원본 바이트가 다르면 빌드 불일치로 중단.*

**명시적으로 포함하지 않음** (V2 §6에서 폐기): city gate `0x13685CD8`, stream gate `0x13686110`, `IsPCPlatform` / IL2CPP `IsPC`, `SwitchUILayoutPlatform` / `ConfirmUILayout`, `OSPROD`, shared `0x16584/0x16808/0x16850`.

### 요구 사항

- Xcode CLT가 설치된 macOS (`codesign`, `xcrun vtool`, `otool`)
- Python 3.8+
- 사용자가 직접 보유한 `com.HoYoverse.Nap_3.1.0_und3fined.ipa` (배포하지 않음)

### 사용법 — IPA 패치

```bash
# ad-hoc (TrollStore / Sideloadly)
python3 tools/patch_zzz_global_ipa.py com.HoYoverse.Nap_3.1.0_und3fined.ipa
# → com.HoYoverse.Nap_3.1.0_und3fined-pcui.ipa (SC_Info 제거, ad-hoc 서명, --verify 통과)

# Apple Developer (개인 서명)
python3 tools/patch_zzz_global_ipa.py input.ipa output.ipa --identity "Apple Development: 홍길동 (TEAMID)"

# 패치만 하고 서명 생략 (테스트)
python3 tools/patch_zzz_global_ipa.py input.ipa --no-sign
```

동작: 경로를 검증하고 메타데이터를 보존하는 `ditto` 압축 해제 → 두 오프셋
바이트 검증 → 패치 → 실행 권한 `0755` 강제 및 entitlement 보존 → 수정하지 않은 중첩 서명은 유지하면서
`SC_Info`만 제거 → `codesign --force --sign <id>` (framework → app 순) →
`codesign --verify --deep --strict` → 원자적 `ditto` 재패키징 → IPA 내부 CRC,
패치 바이트, 실행 권한, 심볼릭 링크, `SC_Info` 제거 재검증. 원본 IPA와 같은
출력 경로는 거부한다.

### 사용법 — PlayTools (선택, macOS PlayCover)

3.1.0 카메라 + F1–F4를 profile-gated 가상 Mouse로 복구. PlayCover를 쓰는 경우에만 설치.

```bash
stage=$(mktemp -d)
ditto -x -k dist/PlayTools-camera-yfix-nullsafe-nocity.framework.zip "$stage"
zsh tools/install_playtools_stage_safely.sh "$stage/PlayTools-camera-yfix.framework"
# ~/Library/Frameworks에만 설치하고 PlayCover Developer ID 서명을 보존하며,
# 해시를 검증한 뒤 _work/install-backups/에 백업
```

이후 PlayCover에서 **Input Compatibility → Unity Native Mouse (Experimental)** 활성화 → 재시작.

macOS 27에서는 PlayCover 3.1.0과 패치된 PlayTools를 합친 앱을 빌드한다.
소스 패치는 심볼릭 링크 번들 대신 관리 컨테이너의 실제 앱 URL을 직접 열어
LaunchServices의 잘못된 `(null)` 권한 오류를 막고, Option/F1–F4/Y-fix가 검증된
PlayTools를 앱 안에 포함한다.

```bash
zsh tools/build_patched_playcover.sh
# → dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip
```

로컬 Developer ID 인증서가 없으므로 통합 앱은 ad-hoc 서명된다. 기존 Developer ID
키체인 ACL이 시작을 멈추지 않도록 이 로컬 빌드에서는 KeyCover를 비활성화하며,
현재 평문 PlayChain DB는 그대로 보존한다. `/Applications/Playcover ZZZ PC UI.app`으로
설치하며 `/Applications/PlayCover.app`을 함께 두지 않는다. 새 앱 창의 일반 ZZZ 실행
버튼이 검증된 실행 경로다.

런처는 `PlayTools-camera-yfix-nullsafe-nocity.framework.zip`을 선택한다. 검증된
Y-fix 바이너리를 layout-2 + MouseInputEnhancement null-safe UnityFramework와
조합하며, 내장 city-gate fingerprint만 원본 분기에 맞춘다. 실패한 city-gate
게임 패치는 설치하지 않는다.

### 릴리즈에서 설치

[Releases](../../releases)에서 다운로드:

- `PlayTools-camera-yfix-nullsafe-nocity.framework.zip`
- `Playcover-ZZZ-PC-UI-3.1.0.app.zip`
- `patch_zzz_global_ipa.py` (또는 이 저장소 clone)

### 현재 상태

- PC UI + 월드 진입: **실기기 검증 완료**
- PlayTools 경유 카메라 + F1–F4: **검증 완료**
- 도시/free-roam 커서 클릭: **실패 확인됨** (C/F 게이트로 미해결; 다음 후보는 `AddDevice` ABI-v3, 아직 미출시)
- 설정 `DTEXT` / 리바인딩: **미해결** (자세한 내용은 V2 문서 참조)

### License

AGPL-3.0 — PlayTools is AGPL-3.0. This repo distributes its patched source under
`src/PlayTools` and the patched framework as a build artifact. Patched IPAs are
not distributed.
