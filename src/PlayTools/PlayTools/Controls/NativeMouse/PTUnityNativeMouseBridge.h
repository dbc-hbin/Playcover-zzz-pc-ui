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

/// Queues one physical keyboard key using the USB HID usage values already
/// produced by PlayTools' NSEvent mapping.
PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseQueueKeyboardHidUsage(
    uint16_t hidUsage,
    bool pressed
);

PT_UNITY_INPUT_INTERNAL bool PTUnityNativeMouseResetKeyboard(void);

PT_UNITY_INPUT_INTERNAL PTUnityNativeMouseStatus PTUnityNativeMouseGetLastStatus(void);
PT_UNITY_INPUT_INTERNAL const char *PTUnityNativeMouseGetSelectedProfileIdentifier(void);

#ifdef __cplusplus
}
#endif

#undef PT_UNITY_INPUT_INTERNAL

#endif /* PTUnityNativeMouseBridge_h */
