#!/usr/bin/env python3
"""Retain matching copyleft dependency sources from the MSYS2 source mirror.

Source archives are data, never executed. Their detached signatures are checked
against the MSYS2 installation's package signing keyring before publication.
GCC runtime libraries use the GCC Runtime Library Exception; remaining bundled
libraries carry permissive notices copied by package-windows.ps1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

DEPENDENCIES = {
    'qt6-base': 'qt6-base',
    'glib2': 'glib2',
    'libiconv': 'libiconv',
    'gettext-runtime': 'gettext',
    'graphite2': 'graphite2',
}

# Fail closed if deployment adds a library whose source/license has not been
# reviewed. Update this list and DEPENDENCIES together when upgrading the SDK.
REVIEWED_DLLS = set('''libb2-1.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll
libdouble-conversion.dll libffi-8.dll libfreetype-6.dll libgcc_s_seh-1.dll
libgio-2.0-0.dll libglib-2.0-0.dll libgmodule-2.0-0.dll libgobject-2.0-0.dll
libgraphite2.dll libharfbuzz-0.dll libiconv-2.dll libicudt78.dll libicuin78.dll
libicuuc78.dll libintl-8.dll libjpeg-8.dll libmd4c.dll libpcre2-16-0.dll
libpcre2-8-0.dll libpng16-16.dll libstdc++-6.dll libwinpthread-1.dll libzstd.dll
Qt6Core.dll Qt6Gui.dll Qt6Network.dll Qt6Widgets.dll zlib1.dll'''.lower().split())


def validate_runtime(package):
    unknown = {p.name.lower() for p in (package / 'bin').glob('*.dll')} - REVIEWED_DLLS
    if unknown:
        raise ValueError('Review licenses and corresponding sources for new DLLs: ' + ', '.join(sorted(unknown)))


def collect(qt_root, output):
    def msys_path(path):
        absolute = path.resolve().as_posix()
        return '/' + absolute[0].lower() + absolute[2:]
    msys = qt_root.parent
    pacman = msys / 'usr/bin/pacman.exe'
    gpg = msys / 'usr/bin/gpg.exe'
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for binary, source in DEPENDENCIES.items():
        package = 'mingw-w64-ucrt-x86_64-' + binary
        version = subprocess.check_output([str(pacman), '-Q', package], text=True).strip().split()[1]
        name = f'mingw-w64-{source}-{version}.src.tar.zst'
        url = 'https://repo.msys2.org/mingw/sources/' + name
        archive = output / name
        signature = output / (name + '.sig')
        for destination, address in ((archive, url), (signature, url + '.sig')):
            if not destination.is_file():
                request = urllib.request.Request(address, headers={'User-Agent': 'GitCanvas-source-collector'})
                temporary = destination.with_suffix(destination.suffix + '.part')
                with urllib.request.urlopen(request, timeout=120) as response, temporary.open('wb') as stream:
                    while chunk := response.read(1024 * 1024):
                        stream.write(chunk)
                temporary.replace(destination)
        verification = subprocess.run([str(gpg), '--batch', '--no-auto-check-trustdb', '--homedir',
                        msys_path(msys / 'etc/pacman.d/gnupg'), '--verify', msys_path(signature), msys_path(archive)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if verification.returncode:
            raise SystemExit(f'Source signature verification failed for {name}:\n' + verification.stderr.decode(errors='replace'))
        records.append({'binaryPackage': package, 'version': version, 'sourceArchive': name,
                        'url': url, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                        'signature': signature.name, 'verification': 'MSYS2 package keyring'})
        print('Verified source:', name, flush=True)
    (output / 'SOURCES.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-root', type=Path, default=Path('C:/msys64/ucrt64'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--package', type=Path)
    args = parser.parse_args()
    if args.package:
        validate_runtime(args.package)
    collect(args.qt_root.resolve(), args.output.resolve())
