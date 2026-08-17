#ifndef PTUnityNativeMouseBridge_h
#define PTUnityNativeMouseBridge_h

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PT_UNITY_INPUT_INTERNAL __attribute__((visibility("hidden")))

typedef enum {
    PTUnityNativeMouseStatusNeverTried = 0,
    PTUnityNativeMouseStatusReady,
    PTUnityNativeMouseStatusWrongThread,
    PTUnityNativeMouseStatusRateLimited,
    PTUnityNativeMouseStatusBusy,
    PTUnityNativeMouseStatusUnsupportedApp,
    PTUnityNativeMouseStatusProfileSchemaMismatch,
    PTUnityNativeMouseStatusWrongBuild,
    PTUnityNativeMouseStatusRuntimeNotReady,
    PTUnityNativeMouseStatusLayoutNotReady,
    PTUnityNativeMouseStatusStringCreationFailed,
    PTUnityNativeMouseStatusAddDeviceFailed,
    PTUnityNativeMouseStatusMousePublicationFailed,
    PTUnityNativeMouseStatusNativeMouseAlreadyPresent,
    PTUnityNativeMouseStatusInvalidDevice,
    PTUnityNativeMouseStatusStaleDevice,
    PTUnityNativeMouseStatusTimeUnavailable,
    PTUnityNativeMouseStatusInvalidArgument,
    PTUnityNativeMouseStatusPermanentlyDisabled,
    PTUnityNativeMouseStatusKeyboardAddDeviceFailed,
    PTUnityNativeMouseStatusInvalidKeyboardDevice,
    PTUnityNativeMouseStatusUnsupportedKeyboardKey,
    PTUnityNativeMouseStatusReadyWithoutKeyboard,
} PTUnityNativeMouseStatus;

/// Selects an embedded, reviewed profile by application bundle identifier.
/// Arbitrary addresses are intentionally not accepted from settings.
PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseSelectProfile(
    const char *bundleIdentifier
);

PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseTryInitialize(void);

PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseQueueState(
    float positionX,
    float positionY,
    float deltaX,
    float deltaY,
    float scrollX,
    float scrollY,
    uint16_t buttons,
    uint16_t clickCount
);

/// Queues one supported ordinary key as a byte delta on Unity's existing
/// physical Keyboard. Host/system modifiers, lock keys, and system keys are
/// rejected so their original AppKit path remains authoritative.
PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseQueueKeyboardHidUsage(
    uint16_t hidUsage,
    bool pressed
);

/// Mirrors a physical key transition without queuing it. This preserves the
/// untouched bits that share a byte with serialized delta events.
PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseObserveKeyboardHidUsage(
    uint16_t hidUsage,
    bool pressed
);

PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseResetKeyboard(void);
PT_UNITY_INPUT_INTERNAL bool PTUnityKeyboardReleaseCorrectionTryInitialize(void);
PT_UNITY_INPUT_INTERNAL bool PTUnityKeyboardReleaseCorrectionDrain(uint8_t backendPressedMask);
PT_UNITY_INPUT_INTERNAL bool PTUnityKeyboardReleaseCorrectionObserveHidUsage(uint16_t hidUsage, bool pressed);

typedef enum {
    PTUnityKeyboardOwnerPassthrough = 0,
    PTUnityKeyboardOwnerConsumed = 1,
    PTUnityKeyboardOwnerFailed = 2,
} PTUnityKeyboardOwnerResult;

/// Synchronously owns one mapped gameplay-key edge. The call performs a fresh
/// Keyboard/front-buffer read-modify-write through Unity UpdateState and a
/// readback check; it never queues or defers an event. F1-F12 and unsupported
/// HID usages return Passthrough so their existing routes remain authoritative.
PT_UNITY_INPUT_INTERNAL bool PTUnityKeyboardOwnerTryInitialize(void);
PT_UNITY_INPUT_INTERNAL PTUnityKeyboardOwnerResult PTUnityKeyboardOwnerHandleHidUsage(
    uint16_t hidUsage,
    bool pressed
);
PT_UNITY_INPUT_INTERNAL void PTUnityKeyboardOwnerReset(void);
PT_UNITY_INPUT_INTERNAL void PTUnityKeyboardReleaseCorrectionSetApplicationActive(bool active);
PT_UNITY_INPUT_INTERNAL void PTUnityKeyboardReleaseCorrectionSetCommandHeld(bool held);
PT_UNITY_INPUT_INTERNAL void PTUnityKeyboardReleaseCorrectionReset(uint32_t reason);
PT_UNITY_INPUT_INTERNAL PTUnityNativeMouseStatus PTUnityKeyboardReleaseCorrectionGetStatus(void);

typedef struct {
    uint64_t sequence;
    uint64_t hookCalls;
    uint64_t hostWDown;
    uint64_t hostWUp;
    uint64_t releaseChecks;
    uint64_t correctionWrites;
    uint64_t drainCalls;
    int32_t hookInstallResult;
    uint32_t lastUpdateType;
    uint8_t lastWBefore;
    uint8_t lastWAfter;
} PTUnityNativeMouseReleaseTrace;

PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseGetReleaseTrace(
    PTUnityNativeMouseReleaseTrace *trace
);

PT_UNITY_INPUT_INTERNAL PTUnityNativeMouseStatus PTUnityNativeMouseGetLastStatus(void);
PT_UNITY_INPUT_INTERNAL const char *PTUnityNativeMouseGetSelectedProfileIdentifier(void);

#ifdef __cplusplus
}
#endif

#undef PT_UNITY_INPUT_INTERNAL

#endif /* PTUnityNativeMouseBridge_h */
