#include "PTUnityNativeMouseBridge.h"
#include "PTUnityNativeMouseProfile.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <math.h>
#include <os/log.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>

#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER) || defined(PLAYTOOLS_KEYBOARD_OWNER)
static bool PTRangeIsReadable(const void *pointer, size_t length);
#include <stdatomic.h>
#endif

enum {
    kSupportedProfileAbiVersion = 5,
    kSupportedEventEncoderVersion = 3,
    kStateEventType = 0x53544154, /* FourCC("STAT") */
    kDeltaStateEventType = 0x444C5441, /* FourCC("DLTA") */
    kMouseStateFormat = 0x4D4F5553, /* FourCC("MOUS") */
    kKeyboardStateFormat = 0x4B455953, /* FourCC("KEYS") */
    kKeyboardStateBytes = 14,
};

typedef void *(*PTPtrToStringAnsi)(const char *value);
typedef void *(*PTInputSystemLoadLayout)(void *layout);
typedef void *(*PTInputSystemAddDevice)(void *layout, void *name, void *variants);
typedef int32_t (*PTInputDeviceGetDeviceId)(void *device);
typedef void *(*PTKeyboardGetCurrent)(const void *methodInfo);
typedef void (*PTInputSystemQueueEvent)(void *eventPointer);
typedef double (*PTNativeInputRuntimeGetCurrentTime)(void *runtime);
typedef void *(*PTFrontBufferForDevice)(int32_t deviceIndex);

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
    uint32_t stateOffset;
    uint8_t stateData[1];
} PTKeyboardDeltaStateEvent;
#pragma pack(pop)

_Static_assert(sizeof(PTInputEvent) == 20, "Unity InputEvent 1.3.0 size changed");
_Static_assert(sizeof(PTMouseState) == 30, "Unity MouseState 1.3.0 size changed");
_Static_assert(sizeof(PTMouseStateEvent) == 54, "Unity StateEvent size changed");
_Static_assert(sizeof(PTKeyboardDeltaStateEvent) == 29,
               "Unity Keyboard DeltaStateEvent size changed");
_Static_assert(offsetof(PTKeyboardDeltaStateEvent, stateFormat) == 20,
               "keyboard delta format offset");
_Static_assert(offsetof(PTKeyboardDeltaStateEvent, stateOffset) == 24,
               "keyboard delta byte offset");
_Static_assert(offsetof(PTKeyboardDeltaStateEvent, stateData) == 28,
               "keyboard delta payload offset");
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
    PTKeyboardGetCurrent keyboardGetCurrent;
    PTInputSystemQueueEvent queueEvent;
    PTNativeInputRuntimeGetCurrentTime getCurrentTime;
    mach_timebase_info_data_t timebase;
} PTBridge;

static PTBridge gBridge = {
    .phase = ATOMIC_VAR_INIT(kPhaseWaiting),
    .lastStatus = ATOMIC_VAR_INIT(PTUnityNativeMouseStatusNeverTried),
    .lastAttemptTicks = ATOMIC_VAR_INIT(0),
};

static bool PTCheckedReadableAddress(
    uintptr_t base,
    uintptr_t offset,
    size_t length,
    void **result
);

#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
typedef void (*PTInvokeAfterUpdateFn)(void *, uint32_t);
typedef bool (*PTUpdateStateFn)(void *, void *, uint32_t, void *, uint32_t, uint32_t, double, void *);
typedef struct {
    const PTUnityNativeMouseProfile *profile;
    uintptr_t unityBase;
    void *manager;
    void *keyboard;
    int32_t keyboardDeviceId;
    PTInputDeviceGetDeviceId getDeviceId;
    PTKeyboardGetCurrent keyboardGetCurrent;
} PTCorrectionContext;
static PTCorrectionContext gCorrection;
static PTInvokeAfterUpdateFn gInvokeOriginal = NULL;
static PTUpdateStateFn gUpdateState = NULL;
static _Atomic bool gHookInstalled = false;
static _Atomic bool gCorrectionReady = false;
static _Atomic bool gCorrectionActive = true;
static _Atomic bool gCorrectionCommand = false;
static _Atomic uint32_t gCorrectionStatus = PTUnityNativeMouseStatusNeverTried;
static _Thread_local bool gHookReentry = false;
static _Atomic uint8_t gHostHeldMask[kKeyboardStateBytes];
static _Atomic uint8_t gReleasedMask[kKeyboardStateBytes];
static _Atomic uint8_t gDeferredReleaseMask[kKeyboardStateBytes];
static _Atomic uint64_t gTraceSequence = 0;
static _Atomic uint64_t gTraceHookCalls = 0;
static _Atomic uint64_t gTraceHostWDown = 0;
static _Atomic uint64_t gTraceHostWUp = 0;
static _Atomic uint64_t gTraceReleaseChecks = 0;
static _Atomic uint64_t gTraceCorrectionWrites = 0;
static _Atomic uint64_t gTraceDrainCalls = 0;
static _Atomic int32_t gTraceHookInstallResult = 0;
static _Atomic uint32_t gTraceLastUpdateType = 0;
static _Atomic uint8_t gTraceLastWBefore = 0;
static _Atomic uint8_t gTraceLastWAfter = 0;

static void PTTraceChanged(void) {
    atomic_fetch_add_explicit(&gTraceSequence, 1, memory_order_release);
}

bool PTUnityNativeMouseGetReleaseTrace(PTUnityNativeMouseReleaseTrace *trace) {
    if (trace == NULL) return false;
    trace->sequence = atomic_load_explicit(&gTraceSequence, memory_order_acquire);
    trace->hookCalls = atomic_load_explicit(&gTraceHookCalls, memory_order_relaxed);
    trace->hostWDown = atomic_load_explicit(&gTraceHostWDown, memory_order_relaxed);
    trace->hostWUp = atomic_load_explicit(&gTraceHostWUp, memory_order_relaxed);
    trace->releaseChecks = atomic_load_explicit(&gTraceReleaseChecks, memory_order_relaxed);
    trace->correctionWrites = atomic_load_explicit(&gTraceCorrectionWrites, memory_order_relaxed);
    trace->drainCalls = atomic_load_explicit(&gTraceDrainCalls, memory_order_relaxed);
    trace->hookInstallResult = atomic_load_explicit(
        &gTraceHookInstallResult, memory_order_relaxed
    );
    trace->lastUpdateType = atomic_load_explicit(&gTraceLastUpdateType, memory_order_relaxed);
    trace->lastWBefore = atomic_load_explicit(&gTraceLastWBefore, memory_order_relaxed);
    trace->lastWAfter = atomic_load_explicit(&gTraceLastWAfter, memory_order_relaxed);
    return true;
}

static void PTClearReleasedMasks(void) {
    for (size_t index = 0; index < kKeyboardStateBytes; ++index) {
        atomic_store_explicit(&gHostHeldMask[index], 0, memory_order_release);
        atomic_store_explicit(&gReleasedMask[index], 0, memory_order_release);
        atomic_store_explicit(&gDeferredReleaseMask[index], 0, memory_order_release);
    }
}

typedef struct {
    uint16_t hidUsage;
    uint8_t byteIndex;
    uint8_t mask;
} PTGameplayKey;

static const PTGameplayKey kGameplayKeys[] = {
    {4,  0x01, 0x80}, /* A: Unity Key 15 */
    {7,  0x02, 0x04}, /* D: Unity Key 18 */
    {22, 0x04, 0x02}, /* S: Unity Key 33 */
    {26, 0x04, 0x20}, /* W: Unity Key 37 */
};

static const PTGameplayKey *PTGameplayKeyForHidUsage(uint16_t hidUsage) {
    for (size_t index = 0; index < sizeof(kGameplayKeys) / sizeof(kGameplayKeys[0]); ++index) {
        if (kGameplayKeys[index].hidUsage == hidUsage) return &kGameplayKeys[index];
    }
    return NULL;
}

static void PTDisableCorrection(PTUnityNativeMouseStatus status) {
    atomic_store_explicit(&gCorrectionReady, false, memory_order_release);
    atomic_store_explicit(&gCorrectionStatus, status, memory_order_release);
    PTClearReleasedMasks();
}

static bool PTHookInstall(void *target, void *replacement, void **original) {
    if (target == NULL || replacement == NULL || original == NULL) {
        atomic_store_explicit(&gTraceHookInstallResult, -1, memory_order_release);
        return false;
    }
    uint8_t *trampoline = mmap(NULL, 32, PROT_READ|PROT_WRITE,
                               MAP_PRIVATE|MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        atomic_store_explicit(&gTraceHookInstallResult, -2, memory_order_release);
        return false;
    }
    uint8_t saved[16];
    memcpy(saved, target, sizeof(saved));
    memcpy(trampoline, saved, sizeof(saved));
    uint32_t *tail = (uint32_t *)(trampoline + 16);
    tail[0] = 0x58000050; tail[1] = 0xD61F0200;
    *(uintptr_t *)(trampoline + 24) = (uintptr_t)target + 16;
    sys_icache_invalidate(trampoline, 32);
    if (mprotect(trampoline, 32, PROT_READ|PROT_EXEC) != 0) {
        atomic_store_explicit(&gTraceHookInstallResult, -3, memory_order_release);
        munmap(trampoline, 32);
        return false;
    }
    /* Publish the callable original before the target can branch to the
       replacement. The hook therefore never observes a NULL trampoline. */
    *original = trampoline;
    uintptr_t page = (uintptr_t)target & ~(uintptr_t)(vm_page_size - 1);
    if ((uintptr_t)target > UINTPTR_MAX - 16 ||
        ((uintptr_t)target + 15) / vm_page_size != page / vm_page_size) {
        atomic_store_explicit(&gTraceHookInstallResult, -4, memory_order_release);
        *original = NULL;
        munmap(trampoline, 32);
        return false;
    }
    vm_address_t regionAddress = (vm_address_t)page;
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t regionInfo = {0};
    mach_msg_type_number_t regionInfoCount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    kern_return_t protectionResult = vm_region_64(
        mach_task_self(), &regionAddress, &regionSize, VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)&regionInfo, &regionInfoCount, &objectName
    );
    if (objectName != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), objectName);
    if (protectionResult != KERN_SUCCESS || regionAddress > page ||
        regionSize < vm_page_size ||
        regionAddress > UINTPTR_MAX - regionSize ||
        regionAddress + regionSize < page + vm_page_size) {
        atomic_store_explicit(
            &gTraceHookInstallResult, 1000 + protectionResult, memory_order_release
        );
        *original = NULL;
        munmap(trampoline, 32);
        return false;
    }
    const vm_prot_t originalProtection = regionInfo.protection;
    protectionResult = vm_protect(
        mach_task_self(), (vm_address_t)page, vm_page_size, FALSE,
        originalProtection | VM_PROT_WRITE | VM_PROT_COPY
    );
    if (protectionResult != KERN_SUCCESS) {
        atomic_store_explicit(
            &gTraceHookInstallResult, 2000 + protectionResult, memory_order_release
        );
        os_log_error(
            OS_LOG_DEFAULT,
            "[PlayTools][ReleaseCorrection] hook_write_protect_failed=%{public}d",
            protectionResult
        );
        *original = NULL;
        munmap(trampoline, 32);
        return false;
    }
    uint32_t *patch = (uint32_t *)target;
    /* Keep the original first instruction live until the destination literal
       and branch are populated, then publish the LDR as the final write. */
    *(uintptr_t *)((uint8_t *)target + 8) = (uintptr_t)replacement;
    patch[1] = 0xD61F0200;
    patch[0] = 0x58000050;
    sys_icache_invalidate(target, 16);
    protectionResult = vm_protect(
        mach_task_self(), (vm_address_t)page, vm_page_size, FALSE,
        originalProtection
    );
    if (protectionResult != KERN_SUCCESS) {
        atomic_store_explicit(
            &gTraceHookInstallResult, 3000 + protectionResult, memory_order_release
        );
        /* The page is still writable. Restore the original entry so a
           partially installed hook can never run with a NULL trampoline. */
        memcpy(target, saved, sizeof(saved));
        sys_icache_invalidate(target, sizeof(saved));
        (void)vm_protect(
            mach_task_self(), (vm_address_t)page, vm_page_size, FALSE,
            originalProtection
        );
        *original = NULL;
        munmap(trampoline, 32);
        return false;
    }
    atomic_store_explicit(&gTraceHookInstallResult, 1, memory_order_release);
    return true;
}

static uint8_t PTGameplayBackendBit(uint32_t byte, uint8_t mask) {
    for (size_t index = 0; index < sizeof(kGameplayKeys) / sizeof(kGameplayKeys[0]); ++index) {
        if (kGameplayKeys[index].byteIndex == byte && kGameplayKeys[index].mask == mask) {
            return (uint8_t)(1u << index);
        }
    }
    return 0;
}

static void PTApplyReleasedMasks(void *manager, uint32_t updateType,
                                 uint8_t backendPressedMask, bool deferNew) {
    uint8_t armed[kKeyboardStateBytes] = {0};
    bool any = false;
    for (size_t index = 0; index < kKeyboardStateBytes; ++index) {
        uint8_t previous = atomic_exchange_explicit(&gDeferredReleaseMask[index], 0, memory_order_acq_rel);
        uint8_t current = atomic_exchange_explicit(&gReleasedMask[index], 0, memory_order_acq_rel);
        if (deferNew) {
            atomic_store_explicit(&gDeferredReleaseMask[index], current, memory_order_release);
        } else {
            previous |= current;
        }
        for (uint8_t bit = 1; bit != 0; bit <<= 1) {
            const uint8_t gameplayBit = PTGameplayBackendBit((uint32_t)index, bit);
            if (gameplayBit != 0 && (backendPressedMask & gameplayBit) != 0) previous &= (uint8_t)~bit;
        }
        armed[index] = previous;
        any |= armed[index] != 0;
    }
    if (!any || !atomic_load_explicit(&gCorrectionReady, memory_order_acquire) ||
        !atomic_load_explicit(&gCorrectionActive, memory_order_acquire) ||
        atomic_load_explicit(&gCorrectionCommand, memory_order_acquire) ||
        manager != gCorrection.manager || gCorrection.profile == NULL ||
        gCorrection.keyboardGetCurrent == NULL || gCorrection.getDeviceId == NULL ||
        gUpdateState == NULL) {
        return;
    }

    void *keyboard = gCorrection.keyboardGetCurrent(NULL);
    if (keyboard == NULL || !PTRangeIsReadable(keyboard, 0x150)) return;
    const int32_t keyboardDeviceId = gCorrection.getDeviceId(keyboard);
    if (keyboardDeviceId <= 0 || keyboardDeviceId > UINT16_MAX) return;
    if (keyboard != gCorrection.keyboard ||
        keyboardDeviceId != gCorrection.keyboardDeviceId) {
        /* A new current Keyboard starts a new ownership generation. Never
           apply a release learned from the previous device. */
        gCorrection.keyboard = keyboard;
        gCorrection.keyboardDeviceId = keyboardDeviceId;
        PTClearReleasedMasks();
        return;
    }

    const int32_t deviceIndex = *(int32_t *)((uintptr_t)keyboard + 0x138);
    const uint32_t stateOffset = *(uint32_t *)((uintptr_t)keyboard + 0xAC);
    if (deviceIndex < 0 || stateOffset > UINT32_MAX - kKeyboardStateBytes) return;
    PTFrontBufferForDevice front = (PTFrontBufferForDevice)(
        gCorrection.unityBase + gCorrection.profile->frontBufferForDeviceRva
    );
    PTNativeInputRuntimeGetCurrentTime timeGetter =
        (PTNativeInputRuntimeGetCurrentTime)(gCorrection.unityBase +
            (updateType == 2 ? gCorrection.profile->nativeInputRuntimeFixedTimeRva :
                               gCorrection.profile->nativeInputRuntimeCurrentTimeRva));
    const double now = timeGetter(NULL);
    if (!isfinite(now)) {
        PTDisableCorrection(PTUnityNativeMouseStatusTimeUnavailable);
        return;
    }

    for (uint32_t byte = 0; byte < kKeyboardStateBytes; ++byte) {
        if (armed[byte] == 0) continue;
        uint8_t *frontState = front == NULL ? NULL : (uint8_t *)front(deviceIndex);
        if (frontState == NULL || (uintptr_t)frontState > UINTPTR_MAX - stateOffset ||
            !PTRangeIsReadable(frontState + stateOffset, kKeyboardStateBytes)) return;
        const uint8_t before = frontState[stateOffset + byte];
        const uint8_t clear = (uint8_t)(armed[byte] & before);
        if (clear == 0) continue;
        uint8_t payload = (uint8_t)(before & (uint8_t)~clear);
        atomic_fetch_add_explicit(&gTraceReleaseChecks, 1, memory_order_relaxed);
        const bool wrote = gUpdateState(
            manager, keyboard, updateType, &payload, byte, 1, now, NULL
        );
        uint8_t *readback = front == NULL ? NULL : (uint8_t *)front(deviceIndex);
        const bool cleared = wrote && readback != NULL &&
            (uintptr_t)readback <= UINTPTR_MAX - stateOffset &&
            PTRangeIsReadable(readback + stateOffset, kKeyboardStateBytes) &&
            (readback[stateOffset + byte] & clear) == 0;
        if (!cleared) {
            PTDisableCorrection(PTUnityNativeMouseStatusInvalidDevice);
            return;
        }
        atomic_fetch_add_explicit(&gTraceCorrectionWrites, 1, memory_order_relaxed);
        if (byte == 4) {
            atomic_store_explicit(&gTraceLastWBefore, before, memory_order_relaxed);
            atomic_store_explicit(&gTraceLastWAfter, readback[stateOffset + byte], memory_order_relaxed);
        }
        PTTraceChanged();
    }
}

static void PTHookAfterUpdate(void *manager, uint32_t updateType) {
    const uint64_t previousHookCalls = atomic_fetch_add_explicit(
        &gTraceHookCalls, 1, memory_order_relaxed
    );
    atomic_store_explicit(&gTraceLastUpdateType, updateType, memory_order_relaxed);
    if (previousHookCalls == 0) {
        PTTraceChanged();
    }
    if (gHookReentry) {
        if (gInvokeOriginal != NULL) {
            gInvokeOriginal(manager, updateType);
        }
        return;
    }
    gHookReentry = true;
    /* Consume armed gameplay releases before Unity's delegate observes the
       frame. W/S share byte 4 and are therefore corrected in one RMW call. */
    if (updateType == 1 || updateType == 2) PTApplyReleasedMasks(manager, updateType, 0, false);
    if (gInvokeOriginal != NULL) gInvokeOriginal(manager, updateType);
    gHookReentry = false;
}

static bool PTInstallReleaseHook(void) {
    if (atomic_load_explicit(&gHookInstalled, memory_order_acquire)) return true;
    if (!gCorrection.profile || !gCorrection.unityBase) return false;
    void *target = (void *)(gCorrection.unityBase +
                            gCorrection.profile->invokeAfterUpdateCallbackRva);
    gUpdateState = (PTUpdateStateFn)(gCorrection.unityBase +
                                    gCorrection.profile->inputSystemUpdateStateRva);
    if (!PTHookInstall(
            target, (void *)&PTHookAfterUpdate, (void **)&gInvokeOriginal
        )) return false;
    atomic_store_explicit(&gHookInstalled, true, memory_order_release);
    atomic_store_explicit(&gCorrectionReady, true, memory_order_release);
    os_log(OS_LOG_DEFAULT, "[PlayTools][ReleaseCorrection] direct_hook=installed");
    return true;
}

#endif

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
        profile->keyboardCurrentRva,
        profile->frontBufferForDeviceRva,
        profile->inputSystemQueueEventRva,
        profile->nativeInputRuntimeCurrentTimeRva,
        profile->invokeAfterUpdateCallbackRva,
        profile->inputSystemUpdateStateRva,
        profile->nativeInputRuntimeFixedTimeRva,
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
    PTKeyboardGetCurrent keyboardGetCurrent =
        (PTKeyboardGetCurrent)(base + profile->keyboardCurrentRva);
    keyboard = keyboardGetCurrent(NULL);
    if (keyboard == NULL || !PTRangeIsReadable(keyboard, 0x150)) {
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

    gBridge.unityBase = base;
    gBridge.manager = manager;
    gBridge.mouse = mouse;
    gBridge.keyboard = keyboard;
    gBridge.deviceId = deviceId;
    gBridge.keyboardDeviceId = keyboardDeviceId;
    memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
    gBridge.addDeviceFailures = 0;
    gBridge.getDeviceId = getDeviceId;
    gBridge.keyboardGetCurrent = keyboardGetCurrent;
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
        gBridge.keyboardGetCurrent = NULL;
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
    if (gBridge.keyboardGetCurrent == NULL || gBridge.getDeviceId == NULL) {
        gBridge.keyboard = NULL;
        gBridge.keyboardDeviceId = 0;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
        return PTReport(PTUnityNativeMouseStatusReadyWithoutKeyboard);
    }
    void *keyboard = gBridge.keyboardGetCurrent(NULL);
    if (keyboard == NULL || !PTRangeIsReadable(keyboard, 0x150)) {
        gBridge.keyboard = NULL;
        gBridge.keyboardDeviceId = 0;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
        return PTReport(PTUnityNativeMouseStatusReadyWithoutKeyboard);
    }
    const int32_t keyboardDeviceId = gBridge.getDeviceId(keyboard);
    if (keyboardDeviceId <= 0 || keyboardDeviceId > UINT16_MAX) {
        gBridge.keyboard = NULL;
        gBridge.keyboardDeviceId = 0;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
        return PTReport(PTUnityNativeMouseStatusInvalidKeyboardDevice);
    }
    if (keyboard != gBridge.keyboard || keyboardDeviceId != gBridge.keyboardDeviceId) {
        gBridge.keyboard = keyboard;
        gBridge.keyboardDeviceId = keyboardDeviceId;
        memset(gBridge.keyboardState, 0, sizeof(gBridge.keyboardState));
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

static bool PTQueueKeyboardByte(double currentTime, uint32_t byteIndex, uint8_t value) {
    if (byteIndex >= kKeyboardStateBytes) {
        return PTReport(PTUnityNativeMouseStatusInvalidArgument);
    }
    PTKeyboardDeltaStateEvent event = {0};
    event.baseEvent.type = kDeltaStateEventType;
    event.baseEvent.sizeInBytes = sizeof(event);
    event.baseEvent.deviceId = (uint16_t)gBridge.keyboardDeviceId;
    event.baseEvent.time = currentTime;
    event.stateFormat = kKeyboardStateFormat;
    event.stateOffset = byteIndex;
    event.stateData[0] = value;
    gBridge.queueEvent(&event);
    atomic_store_explicit(
        &gBridge.lastStatus, PTUnityNativeMouseStatusReady, memory_order_release
    );
    return true;
}

bool PTUnityNativeMouseObserveKeyboardHidUsage(uint16_t hidUsage, bool pressed) {
    if (pthread_main_np() == 0) {
        return false;
    }
    const int32_t key = PTUnityKeyForHidUsage(hidUsage);
    if (key < 0 || key >= kKeyboardStateBytes * 8) {
        return false;
    }
    const uint8_t mask = (uint8_t)(1u << ((uint32_t)key & 7u));
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    /* Only gameplay WASD transitions are eligible for synchronous release
       correction; modifiers, function keys, and other keys never arm it. */
    const PTGameplayKey *gameplayKey = PTGameplayKeyForHidUsage(hidUsage);
    if (hidUsage == 0x1a) {
        if (pressed) atomic_fetch_add_explicit(&gTraceHostWDown, 1, memory_order_relaxed);
        else atomic_fetch_add_explicit(&gTraceHostWUp, 1, memory_order_relaxed);
        PTTraceChanged();
    }
    if (gameplayKey != NULL &&
        atomic_load_explicit(&gCorrectionReady, memory_order_acquire) &&
        atomic_load_explicit(&gCorrectionActive, memory_order_acquire) &&
        !atomic_load_explicit(&gCorrectionCommand, memory_order_acquire)) {
        const uint32_t byteIndex = gameplayKey->byteIndex;
        const uint8_t bit = gameplayKey->mask;
        if (pressed) {
            atomic_fetch_or_explicit(&gHostHeldMask[byteIndex], bit,
                                     memory_order_release);
            atomic_fetch_and_explicit(&gReleasedMask[byteIndex], (uint8_t)~bit,
                                      memory_order_release);
            atomic_fetch_and_explicit(
                &gDeferredReleaseMask[byteIndex], (uint8_t)~bit,
                memory_order_release
            );
        } else {
            const uint8_t held = atomic_fetch_and_explicit(
                &gHostHeldMask[byteIndex], (uint8_t)~bit, memory_order_acq_rel
            );
            if ((held & bit) != 0) {
                atomic_fetch_or_explicit(&gReleasedMask[byteIndex], bit,
                                         memory_order_release);
            }
        }
    }
#endif
    uint8_t *byte = &gBridge.keyboardState[(uint32_t)key >> 3u];
    if (pressed) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
    return true;
}

bool PTUnityNativeMouseQueueKeyboardHidUsage(uint16_t hidUsage, bool pressed) {
    /* Queue one-byte deltas to the existing physical Keyboard. Creating a
       second Keyboard changes Keyboard.current and can hide held movement
       keys. Host/system modifiers, lock keys, and system keys deliberately
       remain on AppKit's original path. */
    switch (hidUsage) {
        case 57:  /* CapsLock */
        case 70:  /* PrintScreen */
        case 71:  /* ScrollLock */
        case 72:  /* Pause */
        case 83:  /* NumLock */
        case 101: /* ContextMenu */
        case 224: /* LeftControl */
        case 226: /* LeftOption */
        case 227: /* LeftCommand */
        case 228: /* RightControl */
        case 229: /* RightShift */
        case 230: /* RightOption */
        case 231: /* RightCommand */
            return PTReport(PTUnityNativeMouseStatusUnsupportedKeyboardKey);
        default:
            break;
    }
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
    return PTQueueKeyboardByte(currentTime, (uint32_t)key >> 3u,
                               gBridge.keyboardState[(uint32_t)key >> 3u]);
}

bool PTUnityNativeMouseResetKeyboard(void) {
    double currentTime = 0.0;
    if (!PTPrepareKeyboardQueue(&currentTime)) {
        return false;
    }
    gBridge.keyboardState[11] &= 0x3Fu;
    gBridge.keyboardState[12] = 0;
    gBridge.keyboardState[13] &= 0xFCu;
    return PTQueueKeyboardByte(currentTime, 11, gBridge.keyboardState[11]) &&
        PTQueueKeyboardByte(currentTime, 12, gBridge.keyboardState[12]) &&
        PTQueueKeyboardByte(currentTime, 13, gBridge.keyboardState[13]);
}

bool PTUnityKeyboardReleaseCorrectionTryInitialize(void) {
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    if (pthread_main_np() == 0) {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusWrongThread, memory_order_release
        );
        return false;
    }
    if (atomic_load_explicit(&gCorrectionReady, memory_order_acquire)) return true;
    if (gBridge.bundleIdentifier[0] == '\0') {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusUnsupportedApp, memory_order_release
        );
        return false;
    }

    const uintptr_t base = PTFindUnityFrameworkBase();
    if (base == 0) {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusRuntimeNotReady, memory_order_release
        );
        return false;
    }
    uint8_t loadedUuid[16] = {0};
    uint64_t textSize = 0;
    if (!PTReadLoadedImageUuid(base, loadedUuid, &textSize)) {
        PTDisableCorrection(PTUnityNativeMouseStatusWrongBuild);
        return false;
    }
    const PTUnityNativeMouseProfile *profile = PTUnityNativeMouseFindProfile(
        gBridge.bundleIdentifier, loadedUuid
    );
    if (profile == NULL ||
        profile->profileAbiVersion != kSupportedProfileAbiVersion ||
        profile->eventEncoderVersion != kSupportedEventEncoderVersion ||
        !PTValidateProfile(base, profile, textSize)) {
        PTDisableCorrection(PTUnityNativeMouseStatusWrongBuild);
        return false;
    }

    void *rootSlotAddress = NULL;
    if (!PTCheckedReadableAddress(
            base, profile->il2cppRootSlotRva, sizeof(void *), &rootSlotAddress
        )) {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusRuntimeNotReady, memory_order_release
        );
        return false;
    }
    void *root = *(void **)rootSlotAddress;
    const uintptr_t managerOffset = profile->inputSystemManagerOffset;
    if ((uintptr_t)root > UINTPTR_MAX - managerOffset ||
        managerOffset > SIZE_MAX - sizeof(void *) ||
        !PTRangeIsReadable(root, managerOffset + sizeof(void *))) {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusRuntimeNotReady, memory_order_release
        );
        return false;
    }
    void *manager = *(void **)((uintptr_t)root + managerOffset);
    if (!PTRangeIsReadable(manager, sizeof(void *))) {
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusRuntimeNotReady, memory_order_release
        );
        return false;
    }

    gCorrection.profile = profile;
    gCorrection.unityBase = base;
    gCorrection.manager = manager;
    gCorrection.getDeviceId =
        (PTInputDeviceGetDeviceId)(base + profile->inputDeviceIdRva);
    gCorrection.keyboardGetCurrent =
        (PTKeyboardGetCurrent)(base + profile->keyboardCurrentRva);
    void *keyboard = gCorrection.keyboardGetCurrent(NULL);
    if (keyboard != NULL && PTRangeIsReadable(keyboard, 0x150)) {
        const int32_t deviceId = gCorrection.getDeviceId(keyboard);
        if (deviceId > 0 && deviceId <= UINT16_MAX) {
            gCorrection.keyboard = keyboard;
            gCorrection.keyboardDeviceId = deviceId;
        }
    }
    if (!PTInstallReleaseHook()) {
        /* Hardened Catalyst denies writes to UnityFramework __TEXT. The
           synchronous main-drain fallback uses the same verified UpdateState
           workhorse without QueueEvent or code-page mutation. */
        if (atomic_load_explicit(
                &gTraceHookInstallResult, memory_order_acquire
            ) == 2000 + KERN_PROTECTION_FAILURE && gUpdateState != NULL) {
            atomic_store_explicit(&gCorrectionReady, true, memory_order_release);
            atomic_store_explicit(
                &gCorrectionStatus, PTUnityNativeMouseStatusReady, memory_order_release
            );
            os_log(
                OS_LOG_DEFAULT,
                "[PlayTools][ReleaseCorrection] fallback=main_drain"
            );
            return true;
        }
        atomic_store_explicit(
            &gCorrectionStatus, PTUnityNativeMouseStatusRuntimeNotReady, memory_order_release
        );
        return false;
    }
    atomic_store_explicit(
        &gCorrectionStatus,
        gCorrection.keyboard == NULL ? PTUnityNativeMouseStatusReadyWithoutKeyboard :
                                       PTUnityNativeMouseStatusReady,
        memory_order_release
    );
    return true;
#else
    return false;
#endif
}

bool PTUnityKeyboardReleaseCorrectionDrain(uint8_t backendPressedMask) {
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    if (pthread_main_np() == 0 ||
        !atomic_load_explicit(&gCorrectionReady, memory_order_acquire)) return false;
    if (atomic_load_explicit(&gHookInstalled, memory_order_acquire)) return true;
    atomic_fetch_add_explicit(&gTraceDrainCalls, 1, memory_order_relaxed);
    PTApplyReleasedMasks(gCorrection.manager, 1, backendPressedMask, true);
    return true;
#else
    (void)backendPressedMask;
    return false;
#endif
}

bool PTUnityKeyboardReleaseCorrectionObserveHidUsage(uint16_t hidUsage, bool pressed) {
    return PTUnityNativeMouseObserveKeyboardHidUsage(hidUsage, pressed);
}

void PTUnityKeyboardReleaseCorrectionSetApplicationActive(bool active) {
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    atomic_store_explicit(&gCorrectionActive, active, memory_order_release);
    if (!active) PTClearReleasedMasks();
#else
    (void)active;
#endif
}
void PTUnityKeyboardReleaseCorrectionSetCommandHeld(bool held) {
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    atomic_store_explicit(&gCorrectionCommand, held, memory_order_release);
    if (held) PTClearReleasedMasks();
#else
    (void)held;
#endif
}
void PTUnityKeyboardReleaseCorrectionReset(uint32_t reason) {
    (void)reason;
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    PTClearReleasedMasks();
#endif
}
PTUnityNativeMouseStatus PTUnityKeyboardReleaseCorrectionGetStatus(void) {
#if defined(PLAYTOOLS_RELEASE_CORRECTION) || defined(PLAYTOOLS_KEYBOARD_OWNER)
    return (PTUnityNativeMouseStatus)atomic_load_explicit(
        &gCorrectionStatus, memory_order_acquire
    );
#else
    return PTUnityNativeMouseStatusNeverTried;
#endif
}

/* Synchronous upstream owner. This is deliberately separate from the legacy
 * release-correction machinery: no pending masks, queue events, GC polling,
 * or code-page hooks are involved. The owner is enabled only in the
 * experimental owner build (which also supplies the reviewed profile/runtime
 * context above). */
#if defined(PLAYTOOLS_KEYBOARD_OWNER)
typedef enum { kOwnerArmedUp = 0, kOwnerOwnedDown, kOwnerPassthroughHeld } PTOwnerCycle;
static PTOwnerCycle gOwnerCycles[256];
static _Atomic bool gOwnerReady = false;

static bool PTOwnerKeyForHid(uint16_t hid, uint8_t *byte, uint8_t *mask, uint16_t *key) {
    /* Keep the synchronous owner deliberately narrow.  These are the only
     * gameplay keys whose release timing needs correction; every other key
     * (including menu/interact bindings) stays on PlayTools' established
     * AppKit/GameController path.  Values are USB HID usages: A=4, D=7,
     * E=8, Q=20, S=22, W=26, Space=44, Left Shift=225. */
    switch (hid) {
        case 4: case 7: case 8: case 20: case 22: case 26: case 44: case 225:
            break;
        default:
            return false;
    }
    int32_t mapped = PTUnityKeyForHidUsage(hid);
    if (mapped < 0) return false;
    uint16_t unityKey = (uint16_t)mapped;
    if (unityKey >= kKeyboardStateBytes * 8u) return false;
    *byte = (uint8_t)(unityKey >> 3u);
    *mask = (uint8_t)(1u << (unityKey & 7u));
    *key = unityKey;
    return true;
}

bool PTUnityKeyboardOwnerTryInitialize(void) {
    if (pthread_main_np() == 0 || gBridge.bundleIdentifier[0] == '\0') return false;
    if (gCorrection.profile == NULL) {
        uintptr_t base = PTFindUnityFrameworkBase();
        uint8_t uuid[16] = {0}; uint64_t textSize = 0;
        if (base == 0 || !PTReadLoadedImageUuid(base, uuid, &textSize)) return false;
        const PTUnityNativeMouseProfile *profile = PTUnityNativeMouseFindProfile(
            gBridge.bundleIdentifier, uuid
        );
        if (profile == NULL || !PTValidateProfile(base, profile, textSize)) return false;
        void *slot = NULL;
        if (!PTCheckedReadableAddress(base, profile->il2cppRootSlotRva, sizeof(void *), &slot)) return false;
        void *root = *(void **)slot;
        if (root == NULL || profile->inputSystemManagerOffset > SIZE_MAX - sizeof(void *) ||
            !PTRangeIsReadable(root, profile->inputSystemManagerOffset + sizeof(void *))) return false;
        void *manager = *(void **)((uintptr_t)root + profile->inputSystemManagerOffset);
        if (!PTRangeIsReadable(manager, sizeof(void *))) return false;
        gCorrection.profile = profile; gCorrection.unityBase = base; gCorrection.manager = manager;
        gCorrection.getDeviceId = (PTInputDeviceGetDeviceId)(base + profile->inputDeviceIdRva);
        gCorrection.keyboardGetCurrent = (PTKeyboardGetCurrent)(base + profile->keyboardCurrentRva);
        gUpdateState = (PTUpdateStateFn)(base + profile->inputSystemUpdateStateRva);
    }
    if (gCorrection.profile == NULL || gCorrection.unityBase == 0 || gUpdateState == NULL ||
        gCorrection.keyboardGetCurrent == NULL || gCorrection.getDeviceId == NULL) {
        return false;
    }
    atomic_store_explicit(&gOwnerReady, true, memory_order_release);
    return true;
}

PTUnityKeyboardOwnerResult PTUnityKeyboardOwnerHandleHidUsage(uint16_t hid, bool pressed) {
    uint8_t byte = 0, mask = 0; uint16_t key = 0;
    if (!PTOwnerKeyForHid(hid, &byte, &mask, &key) ||
        (hid >= 58 && hid <= 69) /* F1-F12 remain supplemental */ ||
        !atomic_load_explicit(&gOwnerReady, memory_order_acquire)) {
        return PTUnityKeyboardOwnerPassthrough;
    }
    PTOwnerCycle *cycle = &gOwnerCycles[key];
    if (!pressed) {
        if (*cycle != kOwnerOwnedDown) { *cycle = kOwnerArmedUp; return PTUnityKeyboardOwnerPassthrough; }
    } else if (*cycle == kOwnerOwnedDown) {
        return PTUnityKeyboardOwnerConsumed; /* repeat */
    } else if (*cycle == kOwnerPassthroughHeld) {
        return PTUnityKeyboardOwnerPassthrough;
    }

    void *keyboard = gCorrection.keyboardGetCurrent(NULL);
    if (keyboard == NULL || !PTRangeIsReadable(keyboard, 0x150)) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    int32_t deviceId = gCorrection.getDeviceId(keyboard);
    if (deviceId <= 0 || deviceId > UINT16_MAX) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    int32_t deviceIndex = *(int32_t *)((uintptr_t)keyboard + 0x138);
    uint32_t stateOffset = *(uint32_t *)((uintptr_t)keyboard + 0xAC);
    if (deviceIndex < 0 || deviceIndex > UINT16_MAX ||
        gCorrection.profile->frontBufferForDeviceRva == 0 ||
        gCorrection.profile->nativeInputRuntimeCurrentTimeRva == 0) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    PTFrontBufferForDevice front = (PTFrontBufferForDevice)(gCorrection.unityBase +
        gCorrection.profile->frontBufferForDeviceRva);
    PTNativeInputRuntimeGetCurrentTime currentTime =
        (PTNativeInputRuntimeGetCurrentTime)(gCorrection.unityBase +
            gCorrection.profile->nativeInputRuntimeCurrentTimeRva);
    uint8_t *state = front == NULL ? NULL : (uint8_t *)front(deviceIndex);
    if (state == NULL || stateOffset > UINT32_MAX - kKeyboardStateBytes ||
        !PTRangeIsReadable(state + stateOffset, kKeyboardStateBytes)) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    uint8_t before = state[stateOffset + byte];
    uint8_t after = pressed ? (uint8_t)(before | mask) : (uint8_t)(before & (uint8_t)~mask);
    double now = currentTime(NULL);
    if (!isfinite(now) || !gUpdateState(gCorrection.manager, keyboard, 1,
                                         &after, byte, 1, now, NULL)) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    state = (uint8_t *)front(deviceIndex);
    if (state == NULL || !PTRangeIsReadable(state + stateOffset, kKeyboardStateBytes) ||
        ((state[stateOffset + byte] & mask) != (pressed ? mask : 0))) {
        *cycle = kOwnerPassthroughHeld; return PTUnityKeyboardOwnerFailed;
    }
    *cycle = pressed ? kOwnerOwnedDown : kOwnerArmedUp;
    return PTUnityKeyboardOwnerConsumed;
}

void PTUnityKeyboardOwnerReset(void) {
    bool failed = false;
    if (atomic_load_explicit(&gOwnerReady, memory_order_acquire)) {
        for (uint16_t key = 0; key < 256; ++key) {
            if (gOwnerCycles[key] == kOwnerOwnedDown) {
                uint16_t hid = 0;
                /* HID lookup is one-to-many; release through the mapped Unity
                 * key by scanning the supported usage space. */
                for (uint16_t usage = 0; usage < 256; ++usage) {
                    uint8_t byte = 0, mask = 0; uint16_t mapped = 0;
                    if (PTOwnerKeyForHid(usage, &byte, &mask, &mapped) && mapped == key) {
                        hid = usage; break;
                    }
                }
                if (hid == 0 || PTUnityKeyboardOwnerHandleHidUsage(hid, false) !=
                    PTUnityKeyboardOwnerConsumed) failed = true;
            }
        }
    }
    memset(gOwnerCycles, 0, sizeof(gOwnerCycles));
    atomic_store_explicit(&gOwnerReady, !failed && atomic_load_explicit(&gOwnerReady, memory_order_acquire), memory_order_release);
}
#else
bool PTUnityKeyboardOwnerTryInitialize(void) { return false; }
PTUnityKeyboardOwnerResult PTUnityKeyboardOwnerHandleHidUsage(uint16_t hid, bool pressed) {
    (void)hid; (void)pressed; return PTUnityKeyboardOwnerPassthrough;
}
void PTUnityKeyboardOwnerReset(void) {}
#endif
