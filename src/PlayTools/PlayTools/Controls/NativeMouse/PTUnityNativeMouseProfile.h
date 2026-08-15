#ifndef PTUnityNativeMouseProfile_h
#define PTUnityNativeMouseProfile_h

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uintptr_t rva;
    uint8_t bytes[16];
} PTUnityCodeFingerprint;

typedef struct {
    const char *identifier;
    const char *bundleIdentifier;
    uint32_t profileAbiVersion;
    uint32_t eventEncoderVersion;
    uint8_t unityFrameworkUuid[16];

    uintptr_t ptrToStringAnsiRva;
    uintptr_t inputSystemLoadLayoutRva;
    uintptr_t inputSystemAddDeviceRva;
    uintptr_t inputDeviceIdRva;
    uintptr_t inputSystemQueueEventRva;
    uintptr_t nativeInputRuntimeCurrentTimeRva;
    uintptr_t il2cppRootSlotRva;
    uintptr_t inputSystemManagerOffset;
    uintptr_t mouseCurrentOffset;

    const PTUnityCodeFingerprint *fingerprints;
    size_t fingerprintCount;
} PTUnityNativeMouseProfile;

#define PT_UNITY_PROFILE_INTERNAL __attribute__((visibility("hidden")))

PT_UNITY_PROFILE_INTERNAL bool PTUnityNativeMouseHasProfileForBundle(
    const char *bundleIdentifier
);

PT_UNITY_PROFILE_INTERNAL const PTUnityNativeMouseProfile *PTUnityNativeMouseFindProfile(
    const char *bundleIdentifier,
    const uint8_t unityFrameworkUuid[16]
);

#undef PT_UNITY_PROFILE_INTERNAL

#endif /* PTUnityNativeMouseProfile_h */
