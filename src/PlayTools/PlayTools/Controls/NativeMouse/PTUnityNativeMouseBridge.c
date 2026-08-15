#include "PTUnityNativeMouseBridge.h"
#include "PTUnityNativeMouseProfile.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

enum {
    kSupportedProfileAbiVersion = 3,
    kSupportedEventEncoderVersion = 2,
    kStateEventType = 0x53544154, /* FourCC("STAT") */
    kMouseStateFormat = 0x4D4F5553, /* FourCC("MOUS") */
    kKeyboardStateFormat = 0x4B455953, /* FourCC("KEYS") */
    kKeyboardStateBytes = 14,
};

typedef void *(*PTPtrToStringAnsi)(const char *value);
typedef void *(*PTInputSystemLoadLayout)(void *layout);
typedef void *(*PTInputSystemAddDevice)(void *layout, void *name, void *variants);
typedef int32_t (*PTInputDeviceGetDeviceId)(void *device);
typedef void (*PTInputSystemQueueEvent)(void *eventPointer);
typedef double (*PTNativeInputRuntimeGetCurrentTime)(void *runtime);

#pragma pack(push, 1)
typedef struct {
    uint32_t type;
    uint16_t sizeInBytes;
    uint16_t deviceId;
    double time;
    uint32_t eventId;
} PTInputEvent;

typedef struct {
    float positionX;
    float positionY;
    float deltaX;
    float deltaY;
    float scrollX;
    float scrollY;
    uint16_t buttons;
    uint16_t displayIndex;
    uint16_t clickCount;
} PTMouseState;

typedef struct {
    PTInputEvent baseEvent;
    uint32_t stateFormat;
    PTMouseState state;
} PTMouseStateEvent;

typedef struct {
    PTInputEvent baseEvent;
    uint32_t stateFormat;
    uint8_t keys[kKeyboardStateBytes];
} PTKeyboardStateEvent;
#pragma pack(pop)

_Static_assert(sizeof(PTInputEvent) == 20, "Unity InputEvent 1.3.0 size changed");
_Static_assert(sizeof(PTMouseState) == 30, "Unity MouseState 1.3.0 size changed");
_Static_assert(sizeof(PTMouseStateEvent) == 54, "Unity StateEvent size changed");
_Static_assert(sizeof(PTKeyboardStateEvent) == 38, "Unity Keyboard StateEvent size changed");
_Static_assert(offsetof(PTMouseStateEvent, stateFormat) == 20, "state format offset");
_Static_assert(offsetof(PTMouseStateEvent, state) == 24, "state payload offset");

typedef enum {
    kPhaseWaiting = 0,
    kPhaseInitializing,
    kPhaseReady,
    kPhasePermanentlyDisabled,
} PTBridgePhase;

typedef struct {
    _Atomic uint32_t phase;
    _Atomic uint32_t lastStatus;
    _Atomic uint64_t lastAttemptTicks;
    uint32_t addDeviceFailures;
    char bundleIdentifier[256];
    const PTUnityNativeMouseProfile *profile;
    uintptr_t unityBase;
    void *manager;
    void *mouse;
    void *keyboard;
    int32_t deviceId;
    int32_t keyboardDeviceId;
    uint8_t keyboardState[kKeyboardStateBytes];
    PTInputDeviceGetDeviceId getDeviceId;
    PTInputSystemQueueEvent queueEvent;
    PTNativeInputRuntimeGetCurrentTime getCurrentTime;
    mach_timebase_info_data_t timebase;
} PTBridge;

static PTBridge gBridge = {
    .phase = ATOMIC_VAR_INIT(kPhaseWaiting),
    .lastStatus = ATOMIC_VAR_INIT(PTUnityNativeMouseStatusNeverTried),
    .lastAttemptTicks = ATOMIC_VAR_INIT(0),
};

static bool PTReport(PTUnityNativeMouseStatus status) {
    atomic_store_explicit(&gBridge.lastStatus, status, memory_order_release);
    return false;
}

static void PTDisablePermanently(PTUnityNativeMouseStatus status) {
    atomic_store_explicit(&gBridge.phase, kPhasePermanentlyDisabled, memory_order_release);
    atomic_store_explicit(&gBridge.lastStatus, status, memory_order_release);
}

static uintptr_t PTFindUnityFrameworkBase(void) {
    static const char suffix[] = "/UnityFramework.framework/UnityFramework";
    const size_t suffixLength = sizeof(suffix) - 1;
    const uint32_t imageCount = _dyld_image_count();
    for (uint32_t index = 0; index < imageCount; ++index) {
        const char *name = _dyld_get_image_name(index);
        if (name == NULL) {
            continue;
        }
        const size_t length = strlen(name);
        if (length >= suffixLength &&
            strcmp(name + length - suffixLength, suffix) == 0) {
            return (uintptr_t)_dyld_get_image_header(index);
        }
    }
    return 0;
}

static bool PTRangeHasProtection(
    const void *pointer,
    size_t length,
    vm_prot_t requiredProtection
) {
    if (pointer == NULL || length == 0) {
        return false;
    }
    vm_address_t regionAddress = (vm_address_t)(uintptr_t)pointer;
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    const kern_return_t result = vm_region_64(
        mach_task_self(),
        &regionAddress,
        &regionSize,
        VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)&info,
        &count,
        &objectName
    );
    if (objectName != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), objectName);
    }
    const uintptr_t start = (uintptr_t)pointer;
    return result == KERN_SUCCESS &&
        (info.protection & requiredProtection) == requiredProtection &&
        regionAddress <= start && regionSize >= length &&
        start - regionAddress <= regionSize - length;
}

static bool PTRangeIsReadable(const void *pointer, size_t length) {
    return PTRangeHasProtection(pointer, length, VM_PROT_READ);
}

static bool PTCheckedReadableAddress(
    uintptr_t base,
    uintptr_t offset,
    size_t length,
    void **result
) {
    if (offset > UINTPTR_MAX - base) {
        return false;
    }
    const uintptr_t address = base + offset;
    if (length > UINTPTR_MAX - address ||
        !PTRangeIsReadable((const void *)address, length)) {
        return false;
    }
    if (result != NULL) {
        *result = (void *)address;
    }
    return true;
}

static bool PTReadLoadedImageUuid(
    uintptr_t base,
    uint8_t uuidBytes[16],
    uint64_t *textSize
) {
    if (!PTRangeIsReadable((const void *)base, sizeof(struct mach_header_64))) {
        return false;
    }
    const struct mach_header_64 *header = (const struct mach_header_64 *)base;
    if (header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_ARM64 ||
        header->ncmds == 0 || header->ncmds > 4096 ||
        header->sizeofcmds < sizeof(struct load_command) ||
        header->sizeofcmds > (1u << 20) ||
        !PTRangeIsReadable(header, sizeof(*header) + header->sizeofcmds)) {
        return false;
    }

    const uint8_t *cursor = (const uint8_t *)(header + 1);
    const uint8_t *commandsEnd = cursor + header->sizeofcmds;
    bool textMatches = false;
    bool uuidFound = false;
    for (uint32_t index = 0; index < header->ncmds; ++index) {
        if (cursor + sizeof(struct load_command) > commandsEnd) {
            return false;
        }
        const struct load_command *command = (const struct load_command *)cursor;
        if (command->cmdsize < sizeof(*command) || cursor + command->cmdsize > commandsEnd) {
            return false;
        }
        if (command->cmd == LC_UUID && command->cmdsize >= sizeof(struct uuid_command)) {
            const struct uuid_command *uuid = (const struct uuid_command *)cursor;
            memcpy(uuidBytes, uuid->uuid, 16);
            uuidFound = true;
        } else if (command->cmd == LC_SEGMENT_64 &&
                   command->cmdsize >= sizeof(struct segment_command_64)) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)cursor;
            if (strncmp(segment->segname, SEG_TEXT, sizeof(segment->segname)) == 0) {
                textMatches = segment->vmaddr == 0 && segment->vmsize > 0;
                if (textMatches) {
                    *textSize = segment->vmsize;
                }
            }
        }
        cursor += command->cmdsize;
    }
    return uuidFound && textMatches;
}

static bool PTValidateProfile(
    uintptr_t base,
    const PTUnityNativeMouseProfile *profile,
    uint64_t textSize
) {
    if (profile == NULL ||
        profile->identifier == NULL || profile->bundleIdentifier == NULL ||
        profile->profileAbiVersion != kSupportedProfileAbiVersion ||
        profile->eventEncoderVersion != kSupportedEventEncoderVersion ||
        profile->fingerprints == NULL || profile->fingerprintCount == 0) {
        return false;
    }
    const uintptr_t requiredRvas[] = {
        profile->ptrToStringAnsiRva,
        profile->inputSystemLoadLayoutRva,
        profile->inputSystemAddDeviceRva,
        profile->inputDeviceIdRva,
        profile->inputSystemQueueEventRva,
        profile->nativeInputRuntimeCurrentTimeRva,
    };
    for (size_t index = 0; index < sizeof(requiredRvas) / sizeof(requiredRvas[0]); ++index) {
        if (requiredRvas[index] == 0 || textSize < 16 ||
            requiredRvas[index] > textSize - 16 ||
            !PTCheckedReadableAddress(base, requiredRvas[index], 16, NULL)) {
            return false;
        }
        bool hasFingerprint = false;
        for (size_t fingerprintIndex = 0;
             fingerprintIndex < profile->fingerprintCount;
             ++fingerprintIndex) {
            if (profile->fingerprints[fingerprintIndex].rva == requiredRvas[index]) {
                hasFingerprint = true;
                break;
            }
        }
        if (!hasFingerprint) {
            return false;
        }
    }
    if (profile->il2cppRootSlotRva == 0 || !PTCheckedReadableAddress(
            base, profile->il2cppRootSlotRva, sizeof(void *), NULL
        )) {
        return false;
    }
    for (size_t index = 0; index < profile->fingerprintCount; ++index) {
        const PTUnityCodeFingerprint *fingerprint = &profile->fingerprints[index];
        void *address = NULL;
        if (fingerprint->rva == 0 || textSize < 16 ||
            fingerprint->rva > textSize - 16 ||
            !PTCheckedReadableAddress(base, fingerprint->rva, 16, &address) ||
            memcmp(address, fingerprint->bytes, 16) != 0) {
            return false;
        }
    }
    return true;
}

static float PTSanitizeFloat(float value, float limit) {
    if (!isfinite(value)) {
        return 0.0f;
    }
    return fmaxf(-limit, fminf(value, limit));
}

static int32_t PTUnityKeyForHidUsage(uint16_t usage) {
    if (usage >= 4 && usage <= 29) { /* A-Z */
        return 15 + (int32_t)(usage - 4);
    }
    if (usage >= 30 && usage <= 39) { /* 1-9, 0 */
        return 41 + (int32_t)(usage - 30);
    }
    if (usage >= 58 && usage <= 69) { /* F1-F12 */
        return 94 + (int32_t)(usage - 58);
    }
    if (usage >= 89 && usage <= 97) { /* Numpad 1-9 */
        return 85 + (int32_t)(usage - 89);
    }
    switch (usage) {
        case 40: return 2;   /* Enter */
        case 41: return 60;  /* Escape */
        case 42: return 65;  /* Backspace */
        case 43: return 3;   /* Tab */
        case 44: return 1;   /* Space */
        case 45: return 13;  /* Minus */
        case 46: return 14;  /* Equals */
        case 47: return 11;  /* LeftBracket */
        case 48: return 12;  /* RightBracket */
        case 49: return 10;  /* Backslash */
        case 50: return 106; /* Non-US Backslash -> OEM1 */
        case 51: return 6;   /* Semicolon */
        case 52: return 5;   /* Quote */
        case 53: return 4;   /* Backquote */
        case 54: return 7;   /* Comma */
        case 55: return 8;   /* Period */
        case 56: return 9;   /* Slash */
        case 57: return 72;  /* CapsLock */
        case 70: return 74;  /* PrintScreen */
        case 71: return 75;  /* ScrollLock */
        case 72: return 76;  /* Pause */
        case 73: return 70;  /* Insert */
        case 74: return 68;  /* Home */
        case 75: return 67;  /* PageUp */
        case 76: return 71;  /* Delete */
        case 77: return 69;  /* End */
        case 78: return 66;  /* PageDown */
        case 79: return 62;  /* RightArrow */
        case 80: return 61;  /* LeftArrow */
        case 81: return 64;  /* DownArrow */
        case 82: return 63;  /* UpArrow */
        case 83: return 73;  /* NumLock */
        case 84: return 78;  /* NumpadDivide */
        case 85: return 79;  /* NumpadMultiply */
        case 86: return 81;  /* NumpadMinus */
        case 87: return 80;  /* NumpadPlus */
        case 88: return 77;  /* NumpadEnter */
        case 98: return 84;  /* Numpad0 */
        case 99: return 82;  /* NumpadPeriod */
        case 101: return 59; /* ContextMenu */
        case 103: return 83; /* NumpadEquals */
        case 224: return 55; /* LeftCtrl */
        case 225: return 51; /* LeftShift */
        case 226: return 53; /* LeftAlt */
        case 227: return 57; /* LeftMeta */
        case 228: return 56; /* RightCtrl */
        case 229: return 52; /* RightShift */
        case 230: return 54; /* RightAlt */
        case 231: return 58; /* RightMeta */
        default: return -1;
    }
}

bool PTUnityNativeMouseSelectProfile(const char *bundleIdentifier) {
    if (pthread_main_np() == 0) {
        return PTReport(PTUnityNativeMouseStatusWrongThread);
    }
    if (bundleIdentifier == NULL || bundleIdentifier[0] == '\0') {
        return PTReport(PTUnityNativeMouseStatusInvalidArgument);
    }
    const size_t length = strlen(bundleIdentifier);
    if (length >= sizeof(gBridge.bundleIdentifier)) {
        return PTReport(PTUnityNativeMouseStatusInvalidArgument);
    }
    if (!PTUnityNativeMouseHasProfileForBundle(bundleIdentifier)) {
        PTDisablePermanently(PTUnityNativeMouseStatusUnsupportedApp);
        return false;
    }
    if (atomic_load_explicit(&gBridge.phase, memory_order_acquire) != kPhaseWaiting) {
        return PTReport(PTUnityNativeMouseStatusBusy);
    }
    memcpy(gBridge.bundleIdentifier, bundleIdentifier, length + 1);
    return true;
}

PTUnityNativeMouseStatus PTUnityNativeMouseGetLastStatus(void) {
    return (PTUnityNativeMouseStatus)atomic_load_explicit(
        &gBridge.lastStatus, memory_order_acquire
    );
}

const char *PTUnityNativeMouseGetSelectedProfileIdentifier(void) {
    return gBridge.profile == NULL ? NULL : gBridge.profile->identifier;
}

bool PTUnityNativeMouseTryInitialize(void) {
    if (pthread_main_np() == 0) {
        return PTReport(PTUnityNativeMouseStatusWrongThread);
    }
    const uint32_t phase = atomic_load_explicit(&gBridge.phase, memory_order_acquire);
    if (phase == kPhaseReady) {
        return true;
    }
    if (phase == kPhasePermanentlyDisabled) {
        return false;
    }
    if (phase == kPhaseInitializing) {
        return PTReport(PTUnityNativeMouseStatusBusy);
    }
    if (gBridge.bundleIdentifier[0] == '\0') {
        return PTReport(PTUnityNativeMouseStatusUnsupportedApp);
    }

    mach_timebase_info_data_t timebase = {0};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS ||
        timebase.numer == 0 || timebase.denom == 0) {
        PTDisablePermanently(PTUnityNativeMouseStatusTimeUnavailable);
        return false;
    }
    const uint64_t now = mach_absolute_time();
    const uint64_t lastAttempt = atomic_load_explicit(
        &gBridge.lastAttemptTicks, memory_order_relaxed
    );
    const long double elapsedNanoseconds = lastAttempt == 0 ? 1.0e12L :
        (long double)(now - lastAttempt) * timebase.numer / timebase.denom;
    if (elapsedNanoseconds < 250000000.0L) {
        return PTReport(PTUnityNativeMouseStatusRateLimited);
    }
    atomic_store_explicit(&gBridge.lastAttemptTicks, now, memory_order_relaxed);

    uint32_t expected = kPhaseWaiting;
    if (!atomic_compare_exchange_strong_explicit(
            &gBridge.phase, &expected, kPhaseInitializing,
            memory_order_acq_rel, memory_order_acquire)) {
        return PTReport(PTUnityNativeMouseStatusBusy);
    }

    const uintptr_t base = PTFindUnityFrameworkBase();
    if (base == 0) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }
    uint8_t loadedUuid[16] = {0};
    uint64_t textSize = 0;
    if (!PTReadLoadedImageUuid(base, loadedUuid, &textSize)) {
        PTDisablePermanently(PTUnityNativeMouseStatusWrongBuild);
        return false;
    }
    const PTUnityNativeMouseProfile *profile = PTUnityNativeMouseFindProfile(
        gBridge.bundleIdentifier, loadedUuid
    );
    if (profile == NULL) {
        PTDisablePermanently(PTUnityNativeMouseStatusWrongBuild);
        return false;
    }
    if (profile->profileAbiVersion != kSupportedProfileAbiVersion ||
        profile->eventEncoderVersion != kSupportedEventEncoderVersion) {
        PTDisablePermanently(PTUnityNativeMouseStatusProfileSchemaMismatch);
        return false;
    }
    if (!PTValidateProfile(base, profile, textSize)) {
        PTDisablePermanently(PTUnityNativeMouseStatusWrongBuild);
        return false;
    }
    gBridge.profile = profile;

    void *rootSlotAddress = NULL;
    if (!PTCheckedReadableAddress(
            base, profile->il2cppRootSlotRva, sizeof(void *), &rootSlotAddress
        )) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }
    void **rootSlot = (void **)rootSlotAddress;
    void *root = *rootSlot;
    const uintptr_t largestRootOffset =
        profile->mouseCurrentOffset > profile->inputSystemManagerOffset ?
            profile->mouseCurrentOffset : profile->inputSystemManagerOffset;
    if ((uintptr_t)root > UINTPTR_MAX - largestRootOffset ||
        largestRootOffset > SIZE_MAX - sizeof(void *) ||
        !PTRangeIsReadable(root, largestRootOffset + sizeof(void *))) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }
    void *manager = *(void **)((uintptr_t)root + profile->inputSystemManagerOffset);
    if (!PTRangeIsReadable(manager, sizeof(void *))) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }
    if (*(void **)((uintptr_t)root + profile->mouseCurrentOffset) != NULL) {
        PTDisablePermanently(PTUnityNativeMouseStatusNativeMouseAlreadyPresent);
        return false;
    }

    PTPtrToStringAnsi makeString =
        (PTPtrToStringAnsi)(base + profile->ptrToStringAnsiRva);
    PTInputSystemLoadLayout loadLayout =
        (PTInputSystemLoadLayout)(base + profile->inputSystemLoadLayoutRva);
    PTInputSystemAddDevice addDevice =
        (PTInputSystemAddDevice)(base + profile->inputSystemAddDeviceRva);
    void *mouseLayout = makeString("Mouse");
    if (mouseLayout == NULL) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusStringCreationFailed);
    }
    if (loadLayout(mouseLayout) == NULL) {
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusLayoutNotReady);
    }
    void *mouse = addDevice(mouseLayout, NULL, NULL);
    if (mouse == NULL) {
        ++gBridge.addDeviceFailures;
        if (gBridge.addDeviceFailures >= 3) {
            PTDisablePermanently(PTUnityNativeMouseStatusAddDeviceFailed);
        } else {
            atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
            PTReport(PTUnityNativeMouseStatusAddDeviceFailed);
        }
        return false;
    }
    if (*(void **)((uintptr_t)root + profile->mouseCurrentOffset) != mouse) {
        PTDisablePermanently(PTUnityNativeMouseStatusMousePublicationFailed);
        return false;
    }
    if (!PTRangeIsReadable(mouse, 0x150)) {
        PTDisablePermanently(PTUnityNativeMouseStatusInvalidDevice);
        return false;
    }

    PTInputDeviceGetDeviceId getDeviceId =
        (PTInputDeviceGetDeviceId)(base + profile->inputDeviceIdRva);
    const int32_t deviceId = getDeviceId(mouse);
    if (deviceId <= 0 || deviceId > UINT16_MAX) {
        PTDisablePermanently(PTUnityNativeMouseStatusInvalidDevice);
        return false;
    }

    void *keyboard = NULL;
    int32_t keyboardDeviceId = 0;
    PTUnityNativeMouseStatus readyStatus = PTUnityNativeMouseStatusReady;
    void *keyboardLayout = makeString("Keyboard");
    if (keyboardLayout == NULL || loadLayout(keyboardLayout) == NULL) {
        readyStatus = PTUnityNativeMouseStatusReadyWithoutKeyboard;
    } else {
        keyboard = addDevice(keyboardLayout, NULL, NULL);
        if (keyboard == NULL) {
            readyStatus = PTUnityNativeMouseStatusReadyWithoutKeyboard;
        } else if (!PTRangeIsReadable(keyboard, 0x150)) {
            keyboard = NULL;
            readyStatus = PTUnityNativeMouseStatusReadyWithoutKeyboard;
        } else {
            keyboardDeviceId = getDeviceId(keyboard);
            if (keyboardDeviceId <= 0 || keyboardDeviceId > UINT16_MAX) {
                keyboard = NULL;
                keyboardDeviceId = 0;
                readyStatus = PTUnityNativeMouseStatusReadyWithoutKeyboard;
            }
        }
    }

    gBridge.unityBase = base;
    gBridge.manager = manager;
    gBridge.mouse = mouse;
    gBridge.keyboard = keyboard;
    gBridge.deviceId = deviceId;
    gBridge.keyboardDeviceId = keyboardDeviceId;
    memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
    gBridge.addDeviceFailures = 0;
    gBridge.getDeviceId = getDeviceId;
    gBridge.queueEvent =
        (PTInputSystemQueueEvent)(base + profile->inputSystemQueueEventRva);
    gBridge.getCurrentTime = (PTNativeInputRuntimeGetCurrentTime)(
        base + profile->nativeInputRuntimeCurrentTimeRva
    );
    gBridge.timebase = timebase;
    atomic_store_explicit(&gBridge.phase, kPhaseReady, memory_order_release);
    atomic_store_explicit(
        &gBridge.lastStatus, readyStatus, memory_order_release
    );
    return true;
}

static bool PTPrepareMouseQueue(double *currentTime) {
    if (pthread_main_np() == 0) {
        return PTReport(PTUnityNativeMouseStatusWrongThread);
    }
    if (atomic_load_explicit(&gBridge.phase, memory_order_acquire) != kPhaseReady ||
        gBridge.profile == NULL ||
        gBridge.queueEvent == NULL || gBridge.getCurrentTime == NULL ||
        gBridge.deviceId <= 0 || gBridge.deviceId > UINT16_MAX) {
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }

    const PTUnityNativeMouseProfile *profile = gBridge.profile;
    void *rootSlotAddress = NULL;
    if (!PTCheckedReadableAddress(
            gBridge.unityBase,
            profile->il2cppRootSlotRva,
            sizeof(void *),
            &rootSlotAddress
        )) {
        return PTReport(PTUnityNativeMouseStatusRuntimeNotReady);
    }
    void **rootSlot = (void **)rootSlotAddress;
    void *root = *rootSlot;
    const uintptr_t largestRootOffset =
        profile->mouseCurrentOffset > profile->inputSystemManagerOffset ?
            profile->mouseCurrentOffset : profile->inputSystemManagerOffset;
    if ((uintptr_t)root > UINTPTR_MAX - largestRootOffset ||
        largestRootOffset > SIZE_MAX - sizeof(void *) ||
        !PTRangeIsReadable(root, largestRootOffset + sizeof(void *)) ||
        *(void **)((uintptr_t)root + profile->inputSystemManagerOffset) !=
            gBridge.manager ||
        *(void **)((uintptr_t)root + profile->mouseCurrentOffset) != gBridge.mouse) {
        gBridge.manager = NULL;
        gBridge.mouse = NULL;
        gBridge.keyboard = NULL;
        gBridge.deviceId = 0;
        gBridge.keyboardDeviceId = 0;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
        gBridge.getDeviceId = NULL;
        gBridge.queueEvent = NULL;
        gBridge.getCurrentTime = NULL;
        atomic_store_explicit(&gBridge.phase, kPhaseWaiting, memory_order_release);
        return PTReport(PTUnityNativeMouseStatusStaleDevice);
    }

    *currentTime = gBridge.getCurrentTime(NULL);
    if (!isfinite(*currentTime) || *currentTime < 0.0) {
        return PTReport(PTUnityNativeMouseStatusTimeUnavailable);
    }
    return true;
}

static bool PTPrepareKeyboardQueue(double *currentTime) {
    if (!PTPrepareMouseQueue(currentTime)) {
        return false;
    }
    if (gBridge.keyboard == NULL || gBridge.getDeviceId == NULL ||
        gBridge.keyboardDeviceId <= 0 || gBridge.keyboardDeviceId > UINT16_MAX ||
        !PTRangeIsReadable(gBridge.keyboard, 0x150) ||
        gBridge.getDeviceId(gBridge.keyboard) != gBridge.keyboardDeviceId) {
        gBridge.keyboard = NULL;
        gBridge.keyboardDeviceId = 0;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
        return PTReport(PTUnityNativeMouseStatusReadyWithoutKeyboard);
    }
    return true;
}

bool PTUnityNativeMouseQueueState(
    float positionX,
    float positionY,
    float deltaX,
    float deltaY,
    float scrollX,
    float scrollY,
    uint16_t buttons,
    uint16_t clickCount
) {
    double currentTime = 0.0;
    if (!PTPrepareMouseQueue(&currentTime)) {
        return false;
    }

    PTMouseStateEvent event = {0};
    event.baseEvent.type = kStateEventType;
    event.baseEvent.sizeInBytes = sizeof(event);
    event.baseEvent.deviceId = (uint16_t)gBridge.deviceId;
    event.baseEvent.time = currentTime;
    event.stateFormat = kMouseStateFormat;
    event.state.positionX = PTSanitizeFloat(positionX, 1000000.0f);
    event.state.positionY = PTSanitizeFloat(positionY, 1000000.0f);
    event.state.deltaX = PTSanitizeFloat(deltaX, 32768.0f);
    event.state.deltaY = PTSanitizeFloat(deltaY, 32768.0f);
    event.state.scrollX = PTSanitizeFloat(scrollX, 32768.0f);
    event.state.scrollY = PTSanitizeFloat(scrollY, 32768.0f);
    event.state.buttons = buttons & 0x001Fu;
    event.state.clickCount = clickCount;
    gBridge.queueEvent(&event);
    atomic_store_explicit(
        &gBridge.lastStatus, PTUnityNativeMouseStatusReady, memory_order_release
    );
    return true;
}

static bool PTQueueKeyboardState(double currentTime) {
    PTKeyboardStateEvent event = {0};
    event.baseEvent.type = kStateEventType;
    event.baseEvent.sizeInBytes = sizeof(event);
    event.baseEvent.deviceId = (uint16_t)gBridge.keyboardDeviceId;
    event.baseEvent.time = currentTime;
    event.stateFormat = kKeyboardStateFormat;
    memcpy(event.keys, gBridge.keyboardState, sizeof(event.keys));
    gBridge.queueEvent(&event);
    atomic_store_explicit(
        &gBridge.lastStatus, PTUnityNativeMouseStatusReady, memory_order_release
    );
    return true;
}

bool PTUnityNativeMouseQueueKeyboardHidUsage(uint16_t hidUsage, bool pressed) {
    const int32_t key = PTUnityKeyForHidUsage(hidUsage);
    if (key < 0 || key >= kKeyboardStateBytes * 8) {
        return PTReport(PTUnityNativeMouseStatusUnsupportedKeyboardKey);
    }
    double currentTime = 0.0;
    if (!PTPrepareKeyboardQueue(&currentTime)) {
        return false;
    }
    const uint8_t mask = (uint8_t)(1u << ((uint32_t)key & 7u));
    uint8_t *byte = &gBridge.keyboardState[(uint32_t)key >> 3u];
    if (pressed) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
    return PTQueueKeyboardState(currentTime);
}

bool PTUnityNativeMouseResetKeyboard(void) {
    double currentTime = 0.0;
    if (!PTPrepareKeyboardQueue(&currentTime)) {
        return false;
    }
    memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
    return PTQueueKeyboardState(currentTime);
}
