import Foundation
import UIKit

/// Profile-gated adapter from PlayTools' AppKit plugin to Unity InputSystem.
/// It deliberately owns input only while PlayTools keymapping is disabled.
final class PTUnityNativeMouseInput {
    static let shared = PTUnityNativeMouseInput()

    private let minimumMotionInterval = 1.0 / 125.0

    private var started = false
    private var monitorsInstalled = false
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
            self?.releaseAllButtons()
            self?.releaseCursorCapture()
        }
        NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.restoreCursorCaptureIfNeeded()
        }
        return true
    }

    /// Called by PlayInput's existing main-thread CADisplayLink.
    func tick() {
        guard started, Thread.isMainThread else { return }
        guard PTUnityNativeMouseTryInitialize() else {
            logStatusChangeIfNeeded()
            return
        }
        if !monitorsInstalled {
            installInputMonitors()
            return
        }
        flushMotionIfDue()
    }

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
            swapMode: { [weak self] keycode in
                _ = self?.handleKeyboard(
                    keycode: keycode,
                    pressed: true,
                    isRepeat: false
                )
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
        guard canQueueInput, !isRepeat,
              let rawUsage = KeyCodeNames.mapNSEventVirtualCodeToGCKeyCodeRawValue[keycode],
              rawUsage >= 0, rawUsage <= Int(UInt16.max) else { return false }
        if !PTUnityNativeMouseQueueKeyboardHidUsage(UInt16(rawUsage), pressed) {
            logStatusChangeIfNeeded()
        }
        // Keep the host event visible to UIKit/AppKit. The virtual Keyboard is
        // an additional Unity InputSystem source, not a global shortcut sink.
        return false
    }

    private var canQueueInput: Bool {
        started && monitorsInstalled && Thread.isMainThread &&
            UIApplication.shared.applicationState == .active
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
        guard started, monitorsInstalled, Thread.isMainThread else { return false }
        cursorCaptured.toggle()
        if cursorCaptured {
            restoreCursorCaptureIfNeeded()
        } else {
            buttons = 0
            pendingDelta = .zero
            positionDirty = false
            queueState(scrollX: 0, scrollY: 0)
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
