# Input Experiment Summary

[English](#english) | [한국어](#한국어)

This file preserves only the conclusions needed to avoid repeating discarded experiments. Detailed dumps, browser packets, disassembly logs, temporary builds, and runtime backups have been removed from the workspace.

## English

### Goal and stable boundary

The target is ZZZ Global 3.1.0 with PlayCover 3.1.0 on arm64 Mac Catalyst. The stable build changes only the game-specific PC UI layout and null-safe mouse initialization. It keeps `Application.platform`, `MouseInputEnhancement`, city/stream gates, and `InputManager.OnUpdate` unchanged.

The accepted keyboard path is:

```text
AppKit keyDown/keyUp
  -> USB HID to Unity Key mapping
  -> existing Keyboard.current byte RMW
  -> one-byte DeltaStateEvent
```

It uses one producer and one existing Unity Keyboard. An AppKit edge is consumed only after queue preconditions succeed; otherwise the complete key cycle fails open to the original path. F1–F12 use the same path. Option remains a host control for switching between camera control and the visible cursor.

### Rejected experiments

- **Platform, city, and stream bypasses:** broadened the patch beyond the proven game-owned UI and mouse initialization boundary.
- **Second virtual Keyboard:** competed with the physical `Keyboard.current` and made device ownership ambiguous.
- **Synchronous KeyboardOwner / direct `UpdateState`:** improved some releases but caused concurrent action keys and menu interactions to be swallowed.
- **ReleaseCorrection v1–v3:** immediate, main-queue-delayed, and GCKeyboard-gated releases variously produced movement tails, stopped movement with Shift, swallowed rapid presses, or replayed stale input.
- **InputArbiter ring, generations, pulses, and watchdogs:** became timing-sensitive and could leave keys owned or released at the wrong Unity update boundary.
- **`InvokeAfterUpdate` / `OnUpdate` hooks and code caves:** exact-address research was useful, but runtime code-page protection, ABI, reentrancy, and event-order guarantees were insufficient for a stable release.
- **Separate supplemental F-key routing:** was unnecessary once F1–F12 joined the same serial producer.

### Conclusion

Host key-down/up observations were generally balanced, while losses and tails appeared later in the Catalyst/Unity/game path. The stable solution therefore minimizes producers and timing correction instead of trying to repair releases after the fact.

## 한국어

### 목표와 안정 경계

대상은 arm64 Mac Catalyst의 ZZZ Global 3.1.0과 PlayCover 3.1.0입니다. 안정 빌드는 게임 전용 PC UI 레이아웃과 마우스 null 초기화만 변경합니다. `Application.platform`, `MouseInputEnhancement`, city/stream gate, `InputManager.OnUpdate`는 원본을 유지합니다.

확정된 키보드 경로는 다음과 같습니다.

```text
AppKit keyDown/keyUp
  -> USB HID를 Unity Key로 변환
  -> 기존 Keyboard.current 바이트 RMW
  -> 1바이트 DeltaStateEvent
```

하나의 producer와 기존 Unity Keyboard 하나만 사용합니다. 큐 준비에 성공한 경우에만 AppKit 이벤트를 소비하고, 실패하면 해당 키 cycle 전체를 원래 경로로 통과시킵니다. F1–F12도 같은 경로를 사용합니다. Option은 카메라 조작과 보이는 커서를 전환하는 호스트 키로 남깁니다.

### 폐기한 실험

- **platform·city·stream 우회:** 검증된 게임 전용 UI·마우스 초기화 경계보다 패치 범위가 넓었습니다.
- **두 번째 가상 Keyboard:** 기존 `Keyboard.current`와 경쟁해 장치 소유권이 불명확해졌습니다.
- **동기 KeyboardOwner / 직접 `UpdateState`:** 일부 release는 개선됐지만 동시 전투키와 메뉴 입력이 씹혔습니다.
- **ReleaseCorrection v1–v3:** 즉시·main queue 지연·GCKeyboard gate 방식 모두 이동 잔류, Shift 동시 입력 정지, 연타 누락 또는 오래된 입력 재생이 발생했습니다.
- **InputArbiter ring·generation·pulse·watchdog:** Unity update 경계에 민감했고 키가 잘못 소유되거나 release되는 문제가 남았습니다.
- **`InvokeAfterUpdate` / `OnUpdate` hook과 code cave:** 주소 연구는 유효했지만 코드 페이지 보호, ABI, 재진입, 이벤트 순서를 안정적으로 보장하지 못했습니다.
- **별도 F키 보조 경로:** F1–F12를 동일 직렬 producer에 포함하면서 필요가 없어졌습니다.

### 결론

호스트에서 관측한 key-down/up은 대체로 균형이 맞았고, 누락과 잔류는 그 뒤의 Catalyst/Unity/게임 경로에서 나타났습니다. 따라서 안정 빌드는 release를 사후 보정하지 않고 producer와 타이밍 개입을 최소화합니다.
