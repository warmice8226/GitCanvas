#!/usr/bin/env python3
"""Create clean app sources and a hashed runtime inventory (not a license certification)."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ('src', 'tests', 'scripts', 'packaging', 'assets', 'i18n', 'docs', 'licenses', '.github')
SOURCE_FILES = ('CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', '.gitignore', '.gitattributes')


def source_files(root):
    for name in SOURCE_FILES:
        path = root / name
        if path.is_file():
            yield path
    for name in SOURCE_DIRS:
        for path in sorted((root / name).rglob('*')):
            relative = path.relative_to(root)
            if path.is_symlink():
                raise ValueError(f'Source symlink is not allowed: {relative}')
            if path.is_file() and not any(part in ('__pycache__', '.git') for part in relative.parts) and path.suffix != '.pyc':
                yield path


def create_sources(root, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in source_files(root):
            info = zipfile.ZipInfo('GitCanvas/' + path.relative_to(root).as_posix(), (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (0o100755 if path.suffix == '.sh' else 0o100644) << 16
            data = path.read_bytes()
            if path.suffix == '.sh':
                data = data.replace(b'\r\n', b'\n')
            archive.writestr(info, data)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + '.sha256').write_text(f'{digest}  {output.name}\n', encoding='utf-8')


def inventory(package, output=None):
    files = []
    for path in sorted(package.rglob('*')):
        if path.name == 'runtime-inventory.json':
            continue
        relative = path.relative_to(package).as_posix()
        if path.is_symlink():
            files.append({'path': relative, 'symlink': str(path.readlink())})
        elif path.is_file():
            files.append({'path': relative, 'size': path.stat().st_size,
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (output or package / 'runtime-inventory.json').write_text(json.dumps({
        'schema': 1, 'applicationLicense': 'GPL-3.0-only',
        'externalTools': ['git', 'ssh (optional)', 'git-lfs (optional)'],
        'correspondingSources': 'Publish matching dependency sources too; see THIRD_PARTY_NOTICES.md.',
        'files': files}, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--inventory-output', type=Path)
    args = parser.parse_args()
    if not args.package.is_dir():
        parser.error('Package directory must already exist')
    create_sources(ROOT, args.sources)
    inventory(args.package, args.inventory_output)
