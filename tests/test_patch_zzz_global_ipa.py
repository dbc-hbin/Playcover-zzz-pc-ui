import importlib.util
import pathlib
import tempfile
import unittest
import zipfile


SCRIPT = pathlib.Path(__file__).parents[1] / "tools" / "patch_zzz_global_ipa.py"
SPEC = importlib.util.spec_from_file_location("patch_zzz_global_ipa", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


class PatchIpaTests(unittest.TestCase):
    def setUp(self):
        self.original_patches = PATCHER.PATCHES
        PATCHER.PATCHES = [
            (4, b"ABCD", b"WXYZ", "first"),
            (16, b"1234", b"5678", "second"),
        ]

    def tearDown(self):
        PATCHER.PATCHES = self.original_patches

    def test_patch_and_repack_preserve_launch_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary) / "unzip"
            app = root / "Payload" / "Test.app"
            framework = app / "Frameworks" / "UnityFramework.framework"
            framework.mkdir(parents=True)

            executable = app / "TestExec"
            executable.write_bytes(b"executable")
            executable.chmod(0o755)
            (app / "Info.plist").write_bytes(
                b'<?xml version="1.0" encoding="UTF-8"?>'
                b'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
                b'"http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
                b'<plist version="1.0"><dict><key>CFBundleExecutable</key>'
                b'<string>TestExec</string></dict></plist>'
            )
            (app / "current").symlink_to("TestExec")

            binary = framework / "UnityFramework"
            data = bytearray(b"0" * 64)
            data[4:8] = b"ABCD"
            data[16:20] = b"1234"
            binary.write_bytes(data)
            PATCHER.patch_binary(binary)

            archive = pathlib.Path(temporary) / "patched.ipa"
            symlinks = PATCHER.collect_symlinks(root)
            PATCHER.repack_payload(root / "Payload", archive)
            PATCHER.verify_repacked_ipa(
                archive,
                pathlib.Path("Payload/Test.app"),
                pathlib.Path("Payload/Test.app/TestExec"),
                symlinks,
                require_no_sc_info=False,
            )

            with zipfile.ZipFile(archive) as ipa:
                mode = ipa.getinfo("Payload/Test.app/TestExec").external_attr >> 16
                self.assertTrue(mode & 0o111)
                self.assertIn("Payload/Test.app/current", symlinks)

    def test_sc_info_removal_preserves_code_signatures(self):
        with tempfile.TemporaryDirectory() as temporary:
            payload = pathlib.Path(temporary) / "Payload"
            sc_info = payload / "Test.app" / "SC_Info"
            signature = payload / "Test.app" / "Frameworks" / "Other.framework" / "_CodeSignature"
            sc_info.mkdir(parents=True)
            signature.mkdir(parents=True)
            (sc_info / "data").write_text("drm")
            (signature / "CodeResources").write_text("signature")

            PATCHER.remove_sc_info(payload)

            self.assertFalse(sc_info.exists())
            self.assertTrue((signature / "CodeResources").is_file())

    def test_unityframework_symlink_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = pathlib.Path(temporary)
            root = base / "unzip"
            framework = root / "Payload" / "Test.app" / "Frameworks" / "UnityFramework.framework"
            framework.mkdir(parents=True)
            outside = base / "outside"
            outside.write_bytes(b"do not modify")
            (framework / "UnityFramework").symlink_to(outside)

            with self.assertRaises(SystemExit):
                PATCHER.find_unityframework(root)
            self.assertEqual(outside.read_bytes(), b"do not modify")

    def test_bundle_executable_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary) / "unzip"
            app = root / "Payload" / "Test.app"
            app.mkdir(parents=True)
            outside = root / "Payload" / "outside"
            outside.write_bytes(b"do not modify")
            (app / "Info.plist").write_bytes(
                b'<?xml version="1.0" encoding="UTF-8"?>'
                b'<plist version="1.0"><dict><key>CFBundleExecutable</key>'
                b'<string>../outside</string></dict></plist>'
            )

            with self.assertRaises(SystemExit):
                PATCHER.find_main_executable(app, root)
            self.assertEqual(outside.read_bytes(), b"do not modify")


if __name__ == "__main__":
    unittest.main()
