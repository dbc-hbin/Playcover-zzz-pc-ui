import Foundation
import GameController
import UIKit

#if PLAYTOOLS_W_TRACE
/// Read-only timing probe for the platform keyboard state that feeds Catalyst.
/// It never installs a GameController callback and never queues input.
private final class PTKeyboardLatencyTrace {
    static let shared = PTKeyboardLatencyTrace()

    private let wHidUsage = 26
    private let observationWindow = 1.0
    private var sequence: UInt64 = 0
    private var armedUntil = 0.0
    private var lastGCPressed: Bool?

    private init() {}

    func started() {
        NSLog("[PlayTools][WTrace] stage=READY source=GCKeyboardPolling")
    }

    func observeHostEdge(rawUsage: Int, pressed: Bool) {
        guard rawUsage == wHidUsage else { return }
        if pressed {
            sequence &+= 1
        }
        let now = ProcessInfo.processInfo.systemUptime
        armedUntil = now + observationWindow
        let gcPressed = readGCPressed()
        lastGCPressed = gcPressed
        emit(
            stage: pressed ? "HOST_DOWN" : "HOST_UP",
            now: now,
            gcPressed: gcPressed
        )
    }

    func tick(phase: String) {
        guard armedUntil > 0 else { return }
        let now = ProcessInfo.processInfo.systemUptime
        let gcPressed = readGCPressed()
        if gcPressed != lastGCPressed {
            lastGCPressed = gcPressed
            emit(
                stage: phase + "_" + (gcPressed == true ? "GC_DOWN" :
                    (gcPressed == false ? "GC_UP" : "GC_UNAVAILABLE")),
                now: now,
                gcPressed: gcPressed
            )
        }
        if now >= armedUntil {
            emit(stage: "WINDOW_END", now: now, gcPressed: gcPressed)
            armedUntil = 0
        }
    }

    private func readGCPressed() -> Bool? {
        GCKeyboard.coalesced?.keyboardInput?
            .button(forKeyCode: GCKeyCode.keyW)?.isPressed
    }

    private func emit(stage: String, now: Double, gcPressed: Bool?) {
        let gcValue = gcPressed.map { $0 ? "1" : "0" } ?? "na"
        NSLog(
            "[PlayTools][WTrace] seq=\(sequence) stage=\(stage) " +
                "uptime=\(String(format: "%.9f", now)) " +
                "mach_ns=\(DispatchTime.now().uptimeNanoseconds) " +
                "gc_w=\(gcValue) active=\(UIApplication.shared.applicationState.rawValue)"
        )
    }
}
#endif

// Profile-gated adapter from PlayTools' AppKit plugin to Unity InputSystem.
// It deliberately owns input only while PlayTools keymapping is disabled.
// swiftlint:disable type_body_length
final class PTUnityNativeMouseInput {
    static let shared = PTUnityNativeMouseInput()

    private let minimumMotionInterval = 1.0 / 125.0
    private let functionKeyHidUsageRange = 58...69
    // F1-F12 are kept on the existing supplemental-device route. Option and
    // Command remain AppKit passthrough so cursor-toggle and host shortcuts
    // retain their established behaviour. The C owner applies an explicit
    // gameplay allowlist (A/D/S/W, Q/E, Space, Left Shift); all other
    // ordinary keys therefore remain on the established path too.
    private let passthroughHidUsages: Set<Int> = [57, 71, 83, 226, 227, 230, 231]

    private var started = false
    private var monitorsInstalled = false
    private var lastReleaseTraceSequence = UInt64.max
    private var lastReleaseTraceStatus = UInt32.max
    private var lastReleaseCorrectionStatus = UInt32.max
    private var suspendedForKeymapping = false
    private var buttons: UInt16 = 0
    private var position = CGPoint.zero
    private var pendingDelta = CGVector.zero
    private var positionDirty = false
    private var cursorCaptured = false
    private var cursorHiddenByBridge = false
    private var lastMotionSentAt = 0.0
    private var lastLoggedStatus: UInt32?
    private init() {}

    @discardableResult
    func startIfEligible() -> Bool {
        guard !started,
              PlaySettings.shared.experimentalUnityNativeMouse,
              let bundleIdentifier = Bundle.main.bundleIdentifier,
              !PlaySettings.shared.keymapping else {
            return false
        }
        guard bundleIdentifier.withCString({
            PTUnityNativeMouseSelectProfile($0)
        }) else {
            logStatusChangeIfNeeded()
            return false
        }
        started = true
        NotificationCenter.default.addObserver(
            forName: UIApplication.willResignActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            PTUnityKeyboardOwnerReset()
            self?.releaseAllButtons()
            self?.releaseCursorCapture()
        }
        NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            _ = PTUnityKeyboardOwnerTryInitialize()
            self?.restoreCursorCaptureIfNeeded()
        }
        let center = NotificationCenter.default
        for name in [
            UITextField.textDidBeginEditingNotification,
            UITextView.textDidBeginEditingNotification
        ] {
            center.addObserver(forName: name, object: nil, queue: .main) { _ in
                PTUnityKeyboardOwnerReset()
            }
        }
        for name in [
            UITextField.textDidEndEditingNotification,
            UITextView.textDidEndEditingNotification
        ] {
            center.addObserver(forName: name, object: nil, queue: .main) { _ in
                _ = PTUnityKeyboardOwnerTryInitialize()
            }
        }
        return true
    }

    /// Called by PlayInput's existing main-thread CADisplayLink.
    func tick() {
        guard started, Thread.isMainThread else { return }
#if PLAYTOOLS_W_TRACE
        PTKeyboardLatencyTrace.shared.tick(phase: "PRE_DRAIN")
#endif
        // Keep the direct Unity Keyboard owner ready independently of mouse
        // publication/readiness. Keymapping only suspends mouse ownership.
        _ = PTUnityKeyboardOwnerTryInitialize()
        _ = PTUnityNativeMouseTryInitialize()
        if PlaySettings.shared.keymapping {
            suspendForKeymappingIfNeeded()
            return
        }
        suspendedForKeymapping = false
        if !monitorsInstalled {
            installInputMonitors()
        }
        guard monitorsInstalled else { return }
        flushMotionIfDue()
    }

    func afterMainQueueDrain() {
        guard started, Thread.isMainThread else { return }
#if PLAYTOOLS_W_TRACE
        PTKeyboardLatencyTrace.shared.tick(phase: "POST_DRAIN")
#endif
    }

#if PLAYTOOLS_RELEASE_CORRECTION
    private func writeReleaseTraceIfChanged() {
        var trace = PTUnityNativeMouseReleaseTrace()
        let status = UInt32(PTUnityNativeMouseGetLastStatus().rawValue)
        let correctionStatus = UInt32(
            PTUnityKeyboardReleaseCorrectionGetStatus().rawValue
        )
        guard PTUnityNativeMouseGetReleaseTrace(&trace),
              trace.sequence != lastReleaseTraceSequence ||
              status != lastReleaseTraceStatus ||
              correctionStatus != lastReleaseCorrectionStatus else { return }
        lastReleaseTraceSequence = trace.sequence
        lastReleaseTraceStatus = status
        lastReleaseCorrectionStatus = correctionStatus
        let payload: [String: Any] = [
            "sequence": trace.sequence,
            "hookCalls": trace.hookCalls,
            "hostWDown": trace.hostWDown,
            "hostWUp": trace.hostWUp,
            "releaseChecks": trace.releaseChecks,
            "correctionWrites": trace.correctionWrites,
            "drainCalls": trace.drainCalls,
            "hookInstallResult": trace.hookInstallResult,
            "lastUpdateType": trace.lastUpdateType,
            "lastWBefore": trace.lastWBefore,
            "lastWAfter": trace.lastWAfter,
            "nativeStatus": status,
            "correctionStatus": correctionStatus,
            "timestamp": Date().timeIntervalSince1970
        ]
        guard JSONSerialization.isValidJSONObject(payload),
              let data = try? JSONSerialization.data(withJSONObject: payload, options: [.prettyPrinted]) else { return }
        let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PlayToolsReleaseTrace.json")
        try? data.write(to: url, options: .atomic)
    }
#endif

    private func installInputMonitors() {
        guard !monitorsInstalled, let inputPlugin = AKInterface.shared else { return }
        monitorsInstalled = true

        inputPlugin.setupMouseMoved { [weak self] deltaX, deltaY in
            self?.handleMove(deltaX: deltaX, deltaY: deltaY)
            return self?.cursorCaptured ?? false
        }
        inputPlugin.setupMouseButton(left: true, right: false) { [weak self] id, pressed in
            self?.handleButton(id: id, pressed: pressed) ?? false
        }
        inputPlugin.setupMouseButton(left: false, right: true) { [weak self] id, pressed in
            self?.handleButton(id: id, pressed: pressed) ?? false
        }
        inputPlugin.setupMouseButton(left: false, right: false) { [weak self] id, pressed in
            self?.handleButton(id: id, pressed: pressed) ?? false
        }
        inputPlugin.setupScrollWheel { [weak self] deltaX, deltaY in
            self?.handleScroll(deltaX: deltaX, deltaY: deltaY)
            return false
        }
        inputPlugin.setupKeyboard(
            keyboard: { [weak self] keycode, pressed, isRepeat, _ in
                self?.handleKeyboard(
                    keycode: keycode,
                    pressed: pressed,
                    isRepeat: isRepeat
                ) ?? false
            },
            swapMode: { [weak self] _ in
                _ = self?.toggleCursorCapture()
                return false
            }
        )
        if let profileIdentifier = PTUnityNativeMouseGetSelectedProfileIdentifier() {
            NSLog(
                "[PlayTools][UnityNativeMouse] profile=%@",
                String(cString: profileIdentifier)
            )
        }
#if PLAYTOOLS_W_TRACE
        PTKeyboardLatencyTrace.shared.started()
#endif
        logStatusChangeIfNeeded()
    }

    private func handleMove(deltaX: CGFloat, deltaY: CGFloat) {
        guard canQueueInput else { return }
        updatePosition()
        positionDirty = true
        if cursorCaptured {
            pendingDelta.dx += deltaX
            pendingDelta.dy -= deltaY
        } else {
            // The virtual Mouse remains Unity's current device while the host
            // cursor is visible. Keep its absolute position current, but do
            // not feed camera-relative motion in UI mode.
            pendingDelta = .zero
        }
        flushMotionIfDue()
    }

    private func handleButton(id: Int, pressed: Bool) -> Bool {
        guard canQueueInput, (0...4).contains(id) else { return false }
        updatePosition()
        positionDirty = true
        let mask = UInt16(1) << UInt16(id)
        if pressed {
            buttons |= mask
        } else {
            buttons &= ~mask
        }
        queueState(scrollX: 0, scrollY: 0)
        return false
    }

    private func handleScroll(deltaX: CGFloat, deltaY: CGFloat) {
        guard canQueueInput else { return }
        updatePosition()
        positionDirty = true
        queueState(scrollX: deltaX, scrollY: deltaY)
    }

    private func handleKeyboard(keycode: UInt16, pressed: Bool, isRepeat: Bool) -> Bool {
        guard
              let rawUsage = KeyCodeNames.mapNSEventVirtualCodeToGCKeyCodeRawValue[keycode],
              rawUsage >= 0, rawUsage <= Int(UInt16.max) else { return false }
#if PLAYTOOLS_W_TRACE
        PTKeyboardLatencyTrace.shared.observeHostEdge(rawUsage: rawUsage, pressed: pressed)
#endif
        guard canQueueInput else { return false }
        _ = PTUnityNativeMouseObserveKeyboardHidUsage(UInt16(rawUsage), pressed)
        if functionKeyHidUsageRange.contains(rawUsage) {
            guard !isRepeat else { return false }
            if !PTUnityNativeMouseQueueKeyboardHidUsage(UInt16(rawUsage), pressed) {
                logStatusChangeIfNeeded()
            }
            return false
        }
        if isTextInputActive || passthroughHidUsages.contains(rawUsage) ||
            (rawUsage > 111 && !(224...229).contains(rawUsage)) { return false }
        let result = PTUnityKeyboardOwnerHandleHidUsage(UInt16(rawUsage), pressed)
        if result == PTUnityKeyboardOwnerConsumed {
            return true
        }
        if result == PTUnityKeyboardOwnerFailed { logStatusChangeIfNeeded() }
        return false
    }

    private var canQueueInput: Bool {
        started && monitorsInstalled && !suspendedForKeymapping &&
            !PlaySettings.shared.keymapping && Thread.isMainThread &&
            UIApplication.shared.applicationState == .active
    }

    private var isTextInputActive: Bool {
        func containsEditor(_ view: UIView) -> Bool {
            if view.isFirstResponder &&
                (view is UITextField || view is UITextView || view is UISearchBar) {
                return true
            }
            return view.subviews.contains(where: containsEditor)
        }
        return UIApplication.shared.windows.contains(where: containsEditor)
    }

    private func suspendForKeymappingIfNeeded() {
        guard !suspendedForKeymapping else { return }
        suspendedForKeymapping = true
        PTUnityKeyboardOwnerReset()
        buttons = 0
        pendingDelta = .zero
        positionDirty = false
        cursorCaptured = false
        if monitorsInstalled {
            queueState(scrollX: 0, scrollY: 0)
            if !PTUnityNativeMouseResetKeyboard() {
                logStatusChangeIfNeeded()
            }
        }
        releaseCursorCapture()
    }

    private func flushMotionIfDue() {
        guard canQueueInput,
              positionDirty || pendingDelta.dx != 0 || pendingDelta.dy != 0 else { return }
        let now = ProcessInfo.processInfo.systemUptime
        guard now - lastMotionSentAt >= minimumMotionInterval else { return }
        updatePosition()
        queueState(scrollX: 0, scrollY: 0)
        lastMotionSentAt = now
    }

    private func queueState(scrollX: CGFloat, scrollY: CGFloat) {
        let delta = pendingDelta
        pendingDelta = .zero
        positionDirty = false
        if !PTUnityNativeMouseQueueState(
            Float(position.x),
            Float(position.y),
            Float(delta.dx),
            Float(delta.dy),
            Float(scrollX),
            Float(scrollY),
            buttons,
            0
        ) {
            logStatusChangeIfNeeded()
        }
    }

    /// Converts the AppKit window point to the Unity display's logical,
    /// bottom-left coordinate space while removing aspect-fit letterboxing.
    private func updatePosition() {
        guard let inputPlugin = AKInterface.shared else { return }
        var point = inputPlugin.mousePoint
        let content = inputPlugin.windowContentBounds
        let logicalDisplay = screen.screenRect
        let nativeDisplay = UIScreen.main.nativeBounds
        let display = nativeDisplay.width > 0 && nativeDisplay.height > 0 ?
            nativeDisplay : logicalDisplay
        guard content.width > 0, content.height > 0,
              display.width > 0, display.height > 0 else { return }

        let scale = max(display.width / content.width, display.height / content.height)
        point.x -= content.minX + (content.width - display.width / scale) / 2
        point.y -= content.minY + (content.height - display.height / scale) / 2
        point.x *= scale
        point.y *= scale
        position.x = min(max(point.x, 0), display.width)
        position.y = min(max(point.y, 0), display.height)
    }

    private func toggleCursorCapture() -> Bool {
        guard canQueueInput else { return false }
        cursorCaptured.toggle()
        if cursorCaptured {
            restoreCursorCaptureIfNeeded()
        } else {
            buttons = 0
            pendingDelta = .zero
            positionDirty = false
            queueState(scrollX: 0, scrollY: 0)
            if !PTUnityNativeMouseResetKeyboard() {
                logStatusChangeIfNeeded()
            }
            releaseCursorCapture()
        }
        return true
    }

    private func restoreCursorCaptureIfNeeded() {
        guard cursorCaptured, !cursorHiddenByBridge,
              UIApplication.shared.applicationState == .active,
              let inputPlugin = AKInterface.shared else { return }
        inputPlugin.hideCursor()
        cursorHiddenByBridge = true
        pendingDelta = .zero
        updatePosition()
    }

    private func releaseCursorCapture() {
        guard cursorHiddenByBridge, let inputPlugin = AKInterface.shared else { return }
        inputPlugin.unhideCursor()
        cursorHiddenByBridge = false
    }

    private func releaseAllButtons() {
        guard started, Thread.isMainThread else { return }
        buttons = 0
        PTUnityKeyboardOwnerReset()
        pendingDelta = .zero
        positionDirty = false
        if monitorsInstalled {
            queueState(scrollX: 0, scrollY: 0)
            if !PTUnityNativeMouseResetKeyboard() {
                logStatusChangeIfNeeded()
            }
        }
    }

    private func logStatusChangeIfNeeded() {
        let status = PTUnityNativeMouseGetLastStatus()
        let rawStatus = UInt32(status.rawValue)
        guard status != PTUnityNativeMouseStatusRateLimited,
              status != PTUnityNativeMouseStatusBusy else { return }
        guard rawStatus != lastLoggedStatus else { return }
        lastLoggedStatus = rawStatus
        NSLog("[PlayTools][UnityNativeMouse] status=%u", rawStatus)
    }
}
// swiftlint:enable type_body_length
