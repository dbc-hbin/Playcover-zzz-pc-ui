import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/build_playtools_command_serialization_experimental.sh"
STABLE = ROOT / "tools/build_playtools_serial_keyboard.sh"
SOURCE = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseInput.swift"
BRIDGE = ROOT / "src/PlayTools/PlayTools/Controls/NativeMouse/PTUnityNativeMouseBridge.c"
AK = ROOT / "src/PlayTools/AKPlugin.swift"

class CommandSerializationExperimentalTests(unittest.TestCase):
    def test_builder_is_option_ui_latch_only(self):
        text = SCRIPT.read_text()
        self.assertIn("PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT", text)
        self.assertNotIn("PLAYTOOLS_COMMAND_SERIALIZATION_EXPERIMENT", text)
        self.assertNotIn("PLAYTOOLS_MOUSE_SERIALIZATION_EXPERIMENT", text)
        self.assertIn("_work/experimental-artifacts", text)
        self.assertIn("activation-delay-ms=80", text)
        self.assertIn("OptionUILatchExperimental", text)

    def test_source_routes_option_only_and_no_mouse_serialization(self):
        swift, bridge, ak = SOURCE.read_text(), BRIDGE.read_text(), AK.read_text()
        self.assertNotIn("PLAYTOOLS_MOUSE_SERIALIZATION_EXPERIMENT", swift)
        self.assertNotIn("serialOwnedMouseButtons", swift)
        self.assertIn("#if defined(PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT)", bridge)
        self.assertIn("case 226: return 53", bridge)
        self.assertIn("case 230: return 54", bridge)
        self.assertIn("PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT", ak)
        self.assertIn("event.keyCode == 58 || event.keyCode == 61", ak)
        self.assertIn("!wasOptionPressed && self.optionPressed", ak)
        self.assertNotIn("event.keyCode == 54 || event.keyCode == 55", ak)

    def test_mouse_events_remain_passthrough(self):
        swift = SOURCE.read_text()
        body = swift.split("private func handleButton(id: Int, pressed: Bool) -> Bool", 1)[1].split("private func handleScroll", 1)[0]
        self.assertIn("return false", body)
        self.assertNotIn("beginClickAssist", swift)
        self.assertNotIn("clickAssist", swift)

    def test_latch_has_delayed_activation_and_reset(self):
        swift, script = SOURCE.read_text(), SCRIPT.read_text()
        self.assertIn("optionUiLatch", swift)
        self.assertIn("optionUiActivationDelay", swift)
        self.assertIn("0.080", swift)
        self.assertIn("resetOptionUiLatchState", swift)
        self.assertIn("[PlayTools][OptionUILatch] mode=option-toggle-ui-latch", script)
        self.assertIn("[PlayTools][OptionUILatch] activation-delay-ms=80", script)

    def test_stable_builder_has_no_experimental_conditions(self):
        self.assertNotIn("PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT", STABLE.read_text())

if __name__ == "__main__":
    unittest.main()
