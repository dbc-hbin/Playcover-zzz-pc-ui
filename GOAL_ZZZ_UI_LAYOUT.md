# Goal: 부팅 시 게임 전용 플랫폼 캐시를 PC UI/Input 프로필로 전환

상태: **PC UI/월드 진입 성공. 도시 클릭 C/F 패치는 실패했고 F는 원복. DTEXT·설정 rebind는 미해결**  
최종 갱신: 2026-08-14

현재 기술 결론과 폐기된 가설의 권위 문서:
`_work/static-wide/PC_UI_MOUSE_BRIDGE_CONSOLIDATED_20260813_V2.md`.
이 문서의 아래 초기 “다음 작업”과 실험 기록은 역사적 경과이며 최신 패치 지침이 아니다.

## 목표

Unity의 실제 실행환경은 `IPhonePlayer(4)`로 유지한다. Metal/Gfx/UIKit/센서 초기화를 건드리지 않고, 게임이 이 값을 소비해 만드는 UI·입력 플랫폼 캐시만 PC 프로필로 전환한다.

## 사용자 육안 확인 전제

사용자가 PC판과 모바일판을 직접 비교했으며, **로그인 화면부터 UI가 미묘하게 다르다.**
따라서 PC/mobile UI selector는 로그인 이후 설정 화면이 아니라 로딩 중, 로그인 UI를
생성하기 전에 실행된다. 로그인 화면에 도달한 시점에는 모바일 UI 분기가 이미 끝난
것으로 본다.

이 항목의 출처는 사용자 육안 비교다. selector의 정확한 주소는 아직 code trace로
미확정이며, 이 관찰만으로 특정 함수를 패치 대상으로 단정하지 않는다.

## 현재 확정된 흐름

```text
+388 ms   metadata bootstrap enter
+692 ms   bootstrap success (1)
+870 ms   native iPhone path 0x16850
+870 ms   shared platform store 0x16808, w8=4
+1062 ms  shared getter 0x16584 first return=4
```

90초 원본 trace:

- `0x16584`: 10,526회, 모두 `4`
- AppUtils platform predicates 4종: 0회
- IL2CPP IsPC getter `0x1302c3c`: 0회
- `SwitchUILayoutPlatform 0x66d6818`: 0회
- `ConfirmUILayout 0x66d6864`: 0회

따라서 AppUtils `IsPCPlatform`과 late UI Switch/Confirm은 부팅 최초 결정자가 아니다.

## 보호 경계

다음은 원본 유지:

- shared getter `0x16584`
- shared store `0x16808`
- iPhonePlayer path `0x16850`
- metadata bootstrap `0x137b3944`
- 라이브 UnityFramework SHA-256 `0539ccadf353a40d83d23e15bc669f1fa8d8bcdbbbed12293fcb36abe9afed96`

`0x16850`을 PC 값으로 바꾸는 실험은 Metal/Gfx SIGSEGV를 만들었다. 공유 platform 값을 변경하는 접근은 폐기한다.

## 다음 작업

300초 pre-login trace에서도 AppUtils/IL2CPP IsPC/known game UI method는 0건이었다.
로그인 화면은 로그상 `Login.OverseaAccountLogin pluginui`이며 HoYoverse Platform SDK
native UI가 소유한다.

1. `MHYPluginLoginManager`와 Plugin UI Objective-C method IMP 열거
2. `showPluginUI:parameters:object:` / `registPluginUI:` 호출·인자 trace
3. 로그인 Plugin UI의 platform/device/layout config key 식별
4. PC `HoYoSDK_RegisterPluginUI` 구성과 비교
5. SDK selector와 게임 본체 UI/Input selector의 공통 config/cache를 찾으면 최소 변경
6. 성공하면 PlayTools 없는 독립 Mac Catalyst app prototype으로 결합

실제 trace에서 `MHYPluginUIBridge +showPluginUI:parameters:object:`가 다음을 받았다.

```text
name = Login.OverseaAccountLogin
parameters = 완성된 element descriptor 배열
object = MHYPortePluginAccountLoginView
```

따라서 bridge는 selector가 아니라 표시 경계다. 다음 타깃은
`MHYPortePluginAccountLoginView` 생성/layout과 descriptor 조립 caller
`0x1542a460 <- 0x1558b688 <- 0x155f8d00 ...`이다.

추가 trace로 `MHYPortePluginAccountLoginView.createUIElements`가 iOS 모바일 로그인
descriptor 전체를 직접 조립함을 확인했다. 이 구간의 `UIDevice.userInterfaceIdiom`과
`NSProcessInfo.isiOSAppOnMac` 호출은 0건이다. 존재하는 분기는 PC/mobile layout이 아니라
third-party login capability별 버튼 추가다.

따라서 로그인 화면에는 더 앞단의 단일 PC selector가 없다. PC 로그인 UI는 별도 SDK
구현이며, 게임 본체 PC UI/Input 목표와 분리한다. 이후 게임 본체 분석은 로그인 Plugin
UI가 닫힌 뒤 생성되는 managed UI root와 `EUILayoutPlatform` 경로에 집중한다.

실제 로그인 성공 및 게임 진입 trace에서 AppUtils IsPC는 39회 호출됐지만 모두
`0xc8ffc78`의 multi-worker generated routine에서 발생했다. UI controller/root 경로가
아니며 scoped override도 안전하지 않다. known `0x66d66xx` UI cluster는 0회였다.

따라서 다음 검증은 설정 화면을 실제로 열어 후보 메서드 호출을 재현하는 방식으로 한다.
재현되지 않는 주소를 UI profile selector로 취급하지 않는다.

## Asset/provider 갱신

iOS와 PC base `resources.assets`는 각각 13,124/13,121 objects이며 차이는 iOS 전용
`MonoScript` 3개다. iOS에도 `pc_input_bg_*`, `console_input_bg_*`, `MouseCursorArrow`,
`Keyboard`가 PC판과 같은 이름·개수로 공존한다. 따라서 PC UI 자산 부재가 문제가 아니며
base catalog나 manifest tag가 platform selector라는 가설은 기각한다.

native `NapAssetBundleManager`의 실제 request 경계를 복구하고 원본 120초 trace를 완료했다.
7,756개 신규 bundle request에서 7,286 unique bundle ID와 568 block hash를 관측했다.
provider는 선택 완료된 숫자 `bundleID/blockPathHash/offset`만 받으며 platform/layout 값을
받지 않는다. 따라서 Nap loader는 selector가 아니라 실행 계층이다.

다음 타깃은 특정 game UI를 여는 순간의 `Foundation.Assets.LoadAsset` 앞단 caller다.
logical prefab ID 선택과 native bundle request를 같은 짧은 시간창에서 연결하기 전에는
bundle ID 교체나 provider patch를 하지 않는다.

## 성공 조건

- shared `Application.platform`은 계속 `4`
- Metal/Gfx 초기화 정상
- 로그인 및 게임 실행 정상
- PC UI/layout 표시
- 키보드/마우스 입력과 PC 키 힌트 활성
- 그래픽 설정은 iOS/Metal 호환 프로필 유지

## 패치 승격 조건

원본 호출 trace, game-owned field, UI/Input 소비자 연결, Frida 최소 변경 성공이 모두 확인되기 전에는 파일 패치를 만들지 않는다.

## 2026-08-09 라이브 패치 기록

- 선택 함수 최종 반환: `UnityFramework+0xEC72310`
- 원본: `e00313aa` (`mov x0, x19`)
- 변경: `40008052` (`mov w0, #2`)
- 효과: game-owned effective UI layout을 `2`(PC)로 강제
- 원본 전체 백업: `_work/artifacts/UnityFramework.live-original.0539ccad.backup`
- 적용·롤백 상세: `_work/artifacts/UI_LAYOUT_PC_PATCH.txt`
- 별도 `com.HoYoverse.Nap.pcui` 복제본과 컨테이너는 휴지통으로 이동
- 원본 `com.HoYoverse.Nap` 리소스 컨테이너는 보존

주의: PC layout은 `Foundation.MouseInputEnhancement`도 활성화한다. iOS 런타임의
입력 관리자 `+0x160` 마우스/포인터 상태 객체가 null이면 게임 진입 중
`InitAllState`에서 예외가 발생한다. 이 객체는 다운로드 자산이 아니라 런타임 입력
장치 상태이므로, 원본 리소스 사용 후에도 별도 입력 경로 수정이 필요할 수 있다.

원본 38GB 리소스 컨테이너로 실제 게임 시작을 재검증했으며 동일한 네 개의
`ShaderCustomData` 경로 오류와 `MouseInputEnhancement.InitAllState` null 예외가
재현됐다. 별도 Windows-channel 실험에서는 Shader 경고가 없어도 같은 null 예외가
발생했다. 따라서 Shader 경고는 비치명적이며 직접 blocker는 layout 2가 활성화한
런타임 마우스 장치 상태다.

## 2026-08-10 마우스 경로 최소 패치 및 실제 진입 검증

- `UnityFramework+0xB3943B8`
- 원본: `74a60139` (`strb w20, [x19,#0x69]`)
- 변경: `7fa60139` (`strb wzr, [x19,#0x69]`)
- 효과: `MouseInputEnhancement.SelfEnabled`를 false로 저장하고 게임의 기존
  비활성화/cleanup 경로를 사용한다. `InitAllState` 호출만 NOP하는 방식은
  enabled=true 상태가 남으므로 사용하지 않았다.
- PC layout 반환 패치 `0xEC72310=40008052`는 그대로 유지한다.
- 패치 직전 layout2 라이브 백업:
  `_work/artifacts/UnityFramework.live-layout2-signed.bdb231.backup`
- 정확한 원본 백업:
  `_work/artifacts/UnityFramework.live-original.0539ccad.backup`
- 적용 후 라이브 SHA-256:
  `118ba3f1f08f1a96024d330ae0a9b85047f2f8014a103f78e98f57fe01cfc26c`
- `codesign --verify --deep --strict` 통과

실제 PlayCover 원본 앱 실행 결과(`NAP_20260810_000532.log`):

- PC형 로그인/타이틀 UI 유지
- `CloseLogin` → `Load scene finish` → `OnAllLoadingFadeOut` → `EnterScene` →
  `SceneSwitch 완료`
- 기존 네 개의 `ShaderCustomData` 경고는 남지만 진행을 막지 않음
- `MouseInputEnhancement`/`NullReferenceException`/`Application will quit` 없음
- 실제 월드 화면과 PC 키 힌트(`F1`–`F4`, `Enter`, `M/N/P`, `Tab`) 표시 확인

## 폐기된 방향

- AppUtils `IsPCPlatform=true`
- IL2CPP IsPC getter 강제
- shared `Application.platform` getter/store/writer 변경
- `SwitchUILayoutPlatform`/`ConfirmUILayout` 단독 변경
- `OSPRODiOS→OSPRODWin`을 UI 해법으로 사용
- 임의 platform enum 비교 또는 `mov w0,#N` 패치

## 증거

- 원시 trace: `/tmp/boot-platform-trace.log`
- 재현: `_work/il2cpp-probe/boot-platform-trace.js`, `_work/il2cpp-probe/boot-platform-trace.py`
- 상세 분석: `_work/ANALYSIS_REPORT.md`
- 실행 계획: `_work/NOTES_global_pc_plan.md`

## 대안 배포 경로

PlayCover 설치본은 이미 모든 iOS Mach-O를 Mac Catalyst(platform 6)로 변환·서명한
독립 `.app`이다. PlayCover는 실행 시 `NSWorkspace.openApplication`만 사용한다.
따라서 진짜 native macOS port 대신 **독립 Catalyst app + game-owned PC UI/Input cache
변경 + PlayTools 제거**가 현실적인 대안이다. 라이브 앱이 아닌 disposable copy에서만
검증한다.
