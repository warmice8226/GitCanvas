import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('metadata', Path(__file__).resolve().parents[1] / 'scripts/portable-metadata.py')
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)


class SourcePackageTest(unittest.TestCase):
    def test_source_excludes_build_credentials_and_history(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('src/main.cpp', 'build/token.txt', '.git/config', 'out/old.exe', '.env', 'docs/guide.html', 'CMakeLists.txt'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test')
            output = root / 'out/source.zip'
            metadata.create_sources(root, output)
            first = output.read_bytes()
            metadata.create_sources(root, output)
            self.assertEqual(first, output.read_bytes())
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), {'GitCanvas/src/main.cpp', 'GitCanvas/docs/guide.html', 'GitCanvas/CMakeLists.txt'})

    def test_inventory_detects_file_changes(self):
        import json
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binary = root / 'app.exe'
            binary.write_bytes(b'first')
            metadata.inventory(root)
            initial = json.loads((root / 'runtime-inventory.json').read_text())
            binary.write_bytes(b'second')
            metadata.inventory(root)
            final = json.loads((root / 'runtime-inventory.json').read_text())
            self.assertEqual(len(final['files']), 1)
            self.assertNotEqual(initial['files'][0]['sha256'], final['files'][0]['sha256'])

    def test_shell_sources_have_unix_newlines_and_permissions(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            script = root / 'scripts/build.sh'
            script.parent.mkdir()
            script.write_bytes(b'#!/bin/bash\r\nexit 0\r\n')
            output = root / 'source.zip'
            metadata.create_sources(root, output)
            with zipfile.ZipFile(output) as archive:
                name = 'GitCanvas/scripts/build.sh'
                self.assertNotIn(b'\r', archive.read(name))
                self.assertEqual(archive.getinfo(name).external_attr >> 16, 0o100755)

    def test_new_runtime_dependency_requires_license_review(self):
        spec = importlib.util.spec_from_file_location('sources', Path(__file__).resolve().parents[1] / 'scripts/collect-windows-sources.py')
        sources = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sources)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'bin').mkdir()
            (root / 'bin/Qt6Core.dll').touch()
            sources.validate_runtime(root)
            (root / 'bin/new_dependency.dll').touch()
            with self.assertRaisesRegex(ValueError, 'new_dependency'):
                sources.validate_runtime(root)


if __name__ == '__main__':
    unittest.main()
