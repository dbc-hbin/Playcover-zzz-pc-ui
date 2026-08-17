import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseBridge.c"
INPUT = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseInput.swift"
SCRIPT = ROOT / "tools/build_playtools_serial_keyboard.sh"
COMBINED = ROOT / "tools/build_patched_playcover.sh"
LAUNCHER = ROOT / "tools/launch_zzz_with_patched_playtools.sh"


class PlayToolsSerialKeyboardTests(unittest.TestCase):
    def test_scope_serializes_mapped_keys_and_excludes_right_shift(self):
        swift = INPUT.read_text()
        self.assertNotIn("serialGameplayHidUsages", swift)
        self.assertIn("229", swift.split("passthroughHidUsages", 1)[1])
        bridge = BRIDGE.read_text()
        self.assertIn("const int32_t key = PTUnityKeyForHidUsage(hidUsage)", bridge)

    def test_function_keys_use_the_serial_success_path(self):
        swift = INPUT.read_text()
        self.assertNotIn("functionKeyHidUsageRange", swift)
        self.assertIn("F1-F12 use this same", swift)
        self.assertRegex(
            swift,
            r"if PTUnityNativeMouseQueueKeyboardHidUsage[\s\S]{0,220}return true",
        )

    def test_serial_path_consumes_only_successful_full_cycles(self):
        swift = INPUT.read_text()
        self.assertIn("serialOwnedUsages", swift)
        self.assertIn("serialPassthroughHeldUsages", swift)
        self.assertIn("PTUnityNativeMouseQueueKeyboardHidUsage", swift)
        self.assertRegex(swift, r"if PTUnityNativeMouseQueueKeyboardHidUsage[\s\S]{0,240}return true")

    def test_serial_policy_excludes_modifiers_locks_and_non_keyboard_usage(self):
        swift = INPUT.read_text()
        passthrough = swift.split("passthroughHidUsages", 1)[1]
        for usage in (57, 70, 71, 72, 83, 101, 224, 226, 227, 228, 229, 230, 231):
            self.assertIn(str(usage), passthrough)
        self.assertIn("rawUsage != 225", swift)
        self.assertRegex(swift, r"rawUsage > 111")

    def test_queue_is_one_byte_delta_on_existing_keyboard(self):
        bridge = BRIDGE.read_text()
        self.assertIn("PTKeyboardDeltaStateEvent", bridge)
        self.assertIn("PTQueueKeyboardByte", bridge)
        self.assertIn("keyboardGetCurrent(NULL)", bridge)
        self.assertNotIn('addDevice(keyboardLayout', bridge)

    def test_build_excludes_arbiter_and_after_update_hook(self):
        script = SCRIPT.read_text()
        self.assertNotIn("PLAYTOOLS_INPUT_ARBITER", script)
        self.assertNotIn("PLAYTOOLS_KEYBOARD_OWNER", script)
        self.assertIn("PTHookAfterUpdate", script)
        self.assertIn("PlayTools-SerialKeyboard-nullsafe.framework.zip", script)

    def test_combined_app_and_launcher_pin_the_stable_artifact(self):
        combined = COMBINED.read_text()
        launcher = LAUNCHER.read_text()
        expected = "PlayTools-SerialKeyboard-nullsafe.framework.zip"
        expected_binary = "132701254ba9a4314e53476f702917f28f9dee2928fc427f8c03d16d4a41db96"
        expected_archive = "7ee375ddc1abc21a996251edcf74485cd4595358a273b72b70f12ae44b083df7"
        for source in (combined, launcher):
            self.assertIn(expected, source)
            self.assertIn(expected_binary, source)
            self.assertIn(expected_archive, source)


if __name__ == "__main__":
    unittest.main()
