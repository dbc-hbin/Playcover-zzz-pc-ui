import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parents[1]
KEY_NAMES = ROOT / "src/PlayTools/PlayTools/Keymap/KeyCodeNames.swift"
INPUT = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseInput.swift"
CONTROL_MODE = ROOT / "src/PlayTools/PlayTools/Controls/Frontend/ControlMode.swift"
BRIDGE = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseBridge.c"
PROFILES = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseProfiles.c"


def virtual_to_hid_mapping():
    source = KEY_NAMES.read_text()
    match = re.search(
        r"mapVirtualToGcLiteral:\s*\[UInt16:\s*Int\]\s*=\s*\[(.*?)\n\s*\]",
        source,
        re.DOTALL,
    )
    assert match is not None
    return {
        int(key): int(value)
        for key, value in re.findall(r"^\s*(\d+):\s*(\d+),?\s*$", match.group(1), re.MULTILINE)
    }


class PlayToolsFunctionKeyScopeTests(unittest.TestCase):
    def test_f1_through_f12_have_expected_hid_usages(self):
        mapping = virtual_to_hid_mapping()
        self.assertEqual(
            {code: mapping[code] for code in (122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111)},
            {
                122: 58,
                120: 59,
                99: 60,
                118: 61,
                96: 62,
                97: 63,
                98: 64,
                100: 65,
                101: 66,
                109: 67,
                103: 68,
                111: 69,
            },
        )

    def test_ordinary_keys_are_outside_virtual_keyboard_range(self):
        mapping = virtual_to_hid_mapping()
        for virtual_code in (13, 0, 1, 2, 56, 59):  # W, A, S, D, Shift, Control
            self.assertNotIn(mapping[virtual_code], range(58, 70))

    def test_swift_and_c_route_function_keys_through_serial_queue(self):
        swift = INPUT.read_text()
        bridge = BRIDGE.read_text()
        self.assertNotIn("functionKeyHidUsageRange", swift)
        self.assertIn("F1-F12 use this same", swift)
        self.assertIn("PTUnityNativeMouseQueueKeyboardHidUsage", bridge)
        self.assertRegex(
            swift,
            r"if PTUnityNativeMouseQueueKeyboardHidUsage[\s\S]{0,220}return true",
        )

        swap_mode = swift.split("swapMode:", 1)[1].split(")\n", 1)[0]
        self.assertNotIn("handleKeyboard", swap_mode)

    def test_runtime_keymapping_gate_releases_virtual_state(self):
        swift = INPUT.read_text()
        self.assertIn("!PlaySettings.shared.keymapping && Thread.isMainThread", swift)
        self.assertIn("suspendForKeymappingIfNeeded()", swift)
        self.assertIn("PTUnityNativeMouseResetKeyboard()", swift)

    def test_function_keys_use_physical_keyboard_byte_deltas(self):
        swift = INPUT.read_text()
        bridge = BRIDGE.read_text()
        profiles = PROFILES.read_text()
        self.assertIn("kDeltaStateEventType = 0x444C5441", bridge)
        self.assertIn("event.stateOffset = byteIndex", bridge)
        self.assertIn("keyboard = keyboardGetCurrent(NULL)", bridge)
        self.assertNotIn('makeString("Keyboard")', bridge)
        self.assertNotIn("addDevice(keyboardLayout", bridge)
        self.assertIn(".keyboardCurrentRva = 0x1316C2A0", profiles)

        input_gate = swift.index("guard canQueueInput else")
        observe = swift.index("PTUnityNativeMouseObserveKeyboardHidUsage")
        self.assertNotIn("functionKeyHidUsageRange", swift)
        self.assertLess(input_gate, observe)

    def test_serial_scope_uses_mapper_with_explicit_exclusions(self):
        bridge = BRIDGE.read_text(); swift = INPUT.read_text()
        self.assertNotIn("serialGameplayHidUsages", swift)
        self.assertIn("PTUnityKeyForHidUsage(hidUsage)", bridge)
        for usage in (57, 70, 71, 72, 83, 101, 224, 226, 227, 228, 229, 230, 231):
            self.assertIn(str(usage), swift.split("passthroughHidUsages", 1)[1])
        self.assertIn("passthroughHidUsages", swift)

    def test_serial_keyboard_bypasses_text_input_mode(self):
        control = CONTROL_MODE.read_text(); swift = INPUT.read_text()
        self.assertIn("case textInput", control)
        self.assertIn("ModeAutomaton.onUITextInputBeginEdit()", control)
        self.assertIn("isTextInputActive", swift)
        self.assertIn("UITextField", swift)
        self.assertIn("UITextView", swift)

if __name__ == "__main__":
    unittest.main()
