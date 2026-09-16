import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("bundle", Path(__file__).resolve().parents[1] / "scripts/verify-macos-bundle.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class BundleVerification(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name) / "GitCanvas.app"
        (self.app / "Contents/MacOS").mkdir(parents=True)
        self.exe = self.app / "Contents/MacOS/GitCanvas"
        self.exe.write_bytes(bytes.fromhex("cffaedfe") + b"fixture")
        self.info = self.app / "Contents/Info.plist"
        self.info.write_bytes(plistlib.dumps({"CFBundleIdentifier": "io.github.gitcanvas.GitCanvas", "CFBundleExecutable": "GitCanvas", "CFBundlePackageType": "APPL", "LSMinimumSystemVersion": "15.0"}))

    def runner(self, args, text):
        self.assertTrue(text)
        if args[0] == "lipo":
            return "arm64 x86_64\n"
        return str(self.exe) + ":\n\t@rpath/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"

    def test_valid_architectures_and_system_dependencies(self):
        for arch in ("arm64", "x86_64"):
            self.assertEqual(bundle.verify(self.app, arch, self.runner), 1)
        self.assertFalse(bundle.dependency_allowed("/opt/homebrew/lib/libQt6Core.dylib"))
        self.assertFalse(bundle.dependency_allowed("/usr/local/lib/libQt6Core.dylib"))

    def test_external_library_rejected(self):
        def external(args, text):
            return self.runner(args, text) if args[0] == "lipo" else "app:\n\t/Users/developer/QtCore.dylib (compatibility version 6.0.0)\n"
        with self.assertRaisesRegex(ValueError, "External dependency"):
            bundle.verify(self.app, "arm64", external)

    def test_wrong_architecture_rejected(self):
        def intel(args, text):
            return "x86_64" if args[0] == "lipo" else self.runner(args, text)
        with self.assertRaisesRegex(ValueError, "architecture"):
            bundle.verify(self.app, "arm64", intel)

    def test_wrong_identity_and_binary_rejected(self):
        self.info.write_bytes(plistlib.dumps({"CFBundleIdentifier": "wrong"}))
        with self.assertRaisesRegex(ValueError, "identity"):
            bundle.verify(self.app, "arm64", self.runner)
        self.assertTrue(bundle.is_macho(self.exe))
        self.exe.write_bytes(b"MZ windows executable")
        self.assertFalse(bundle.is_macho(self.exe))

    def test_development_rpath_rejected(self):
        def development(args, text):
            if "-l" in args:
                return "Load command 1\n cmd LC_RPATH\n cmdsize 40\n path /opt/homebrew/lib (offset 12)\n"
            return self.runner(args, text)
        with self.assertRaisesRegex(ValueError, "runtime search path"):
            bundle.verify(self.app, "arm64", development)


if __name__ == "__main__":
    unittest.main()
