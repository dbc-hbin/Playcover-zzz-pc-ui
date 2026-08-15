# ZZZ IPA Keyboard — Global 3.1.0 PC UI Patch / ZZZ 글로벌 3.1.0 PC UI 패치

> **Authority / 권위 문서**: `GOAL_ZZZ_UI_LAYOUT.md` + `_work/static-wide/PC_UI_MOUSE_BRIDGE_CONSOLIDATED_20260813_V2.md`  
> Validated on live device: PC login/title UI + world entry (`CloseLogin → SceneSwitch complete`), no `MouseInputEnhancement` NRE.

---

## English

### What this repo contains

| Path | Description |
|---|---|
| `tools/patch_zzz_global_ipa.py` | **Minimal working patch** — applies exactly 2 verified UnityFramework patches to a user-owned IPA and re-signs it |
| `tools/install_playtools_stage_safely.sh` | Safe installer for the patched PlayTools framework (with rollback) |
| `dist/PlayTools-simplified-citygate.framework` | Final patched PlayTools framework (also as `dist/*.zip` for Releases) |

Only these are committed. Patched IPAs (`*.ipa`, `*.dmg`) are never committed.

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

The script: `unzip` → verify bytes at both offsets → patch → `chmod 755` → remove `SC_Info`/`_CodeSignature` → `codesign --force --sign <id> --timestamp=none` (framework → app) → `codesign --verify --deep --strict` → `zip` repack → verify inside IPA.

### Usage — PlayTools (optional, macOS PlayCover)

Fixes 3.1.0 camera + F1–F4 via profile-gated virtual Mouse. Installed only if you use PlayCover on macOS.

```bash
zsh tools/install_playtools_stage_safely.sh dist/PlayTools-simplified-citygate.framework
# installs only ~/Library/Frameworks, preserves PlayCover's Developer ID signature,
# blocks PlayCover's startup overwrite, verifies hashes, and backs up to _work/install-backups/
```

Then in PlayCover: **Input Compatibility → Unity Native Mouse (Experimental)** → restart.

### Install from Release

Download from [Releases](../../releases):

- `PlayTools-simplified-citygate.framework.zip`
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
| `tools/install_playtools_stage_safely.sh` | 패치된 PlayTools 프레임워크 안전 설치 스크립트 (롤백 지원) |
| `dist/PlayTools-simplified-citygate.framework` | 최종 패치된 PlayTools 프레임워크 (Releases용 `dist/*.zip` 포함) |

이 외에는 커밋하지 않음. 패치된 IPA(`*.ipa`, `*.dmg`)는 절대 커밋하지 않음.

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

동작: `unzip` → 두 오프셋 바이트 검증 → 패치 → `chmod 755` → `SC_Info`/`_CodeSignature` 제거 → `codesign --force --sign <id>` (framework → app 순) → `codesign --verify --deep --strict` → `zip` 재패키징 → IPA 내부 바이트 재검증.

### 사용법 — PlayTools (선택, macOS PlayCover)

3.1.0 카메라 + F1–F4를 profile-gated 가상 Mouse로 복구. PlayCover를 쓰는 경우에만 설치.

```bash
zsh tools/install_playtools_stage_safely.sh dist/PlayTools-simplified-citygate.framework
# ~/Library/Frameworks에만 설치하고 PlayCover Developer ID 서명을 보존하며,
# 시작 시 덮어쓰기를 차단하고 해시를 검증한 뒤 _work/install-backups/에 백업
```

이후 PlayCover에서 **Input Compatibility → Unity Native Mouse (Experimental)** 활성화 → 재시작.

### 릴리즈에서 설치

[Releases](../../releases)에서 다운로드:

- `PlayTools-simplified-citygate.framework.zip`
- `patch_zzz_global_ipa.py` (또는 이 저장소 clone)

### 현재 상태

- PC UI + 월드 진입: **실기기 검증 완료**
- PlayTools 경유 카메라 + F1–F4: **검증 완료**
- 도시/free-roam 커서 클릭: **실패 확인됨** (C/F 게이트로 미해결; 다음 후보는 `AddDevice` ABI-v3, 아직 미출시)
- 설정 `DTEXT` / 리바인딩: **미해결** (자세한 내용은 V2 문서 참조)

### License

AGPL-3.0 — PlayTools is AGPL-3.0. This repo distributes PlayTools source via `playcover-latest` reference and the patched framework as a build artifact. Patched IPAs are not distributed.
