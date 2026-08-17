# Goal: Stable PC UI and Native Input

이 파일은 저장소의 목표 요약입니다. 정확한 현재 배포 사양과 바이트는 [STABLE_BUILD.md](STABLE_BUILD.md)를 권위 문서로 사용합니다.

## 목표

- `Application.platform`과 Metal/UIKit 실행환경은 iPhonePlayer 경로로 유지한다.
- 게임 전용 effective UI layout만 PC 값 `2`로 전환한다.
- `MouseInputEnhancement`를 활성 상태로 유지하면서 null 초기 상태를 안전하게 처리한다.
- PlayTools가 네이티브 Mouse와 기존 Unity Keyboard에 이벤트를 전달한다.
- 키 입력은 하나의 직렬 생산자만 사용하고, 실패 시 AppKit 원본으로 fail-open한다.

## 현재 패치 경계

```text
0xEC72310  PC layout 2
0xB392FF0  null mouse branch
0xB393540  zero mouse baseline
0xB393544  initialization rejoin
```

`MouseInputEnhancement` enable store, city/stream gates, shared platform 값, Unity InputSystem update 함수는 원본을 유지합니다.

## 성공 조건

- 로그인, 월드 진입, PC UI가 정상이다.
- 카메라, 마우스 버튼·스크롤·커서 캡처가 정상이다.
- 일반 키와 F1–F12가 빠른 전투 조합에서도 가능한 한 순서대로 전달된다.
- Option/Command/Control/Right Shift, lock/system 키와 텍스트 입력은 호스트 경로를 유지한다.
- PlayTools와 PlayCover 앱의 코드 서명 및 실제 런타임 매핑이 검증된다.

과거 2-site MouseInputEnhancement-disable 구성과 owner/release/arbiter 실험은 `_work/` 및 연구 문서에만 남는 역사적 기록이며 현재 배포 지침이 아닙니다.
