"""Validate a deployed native macOS bundle before signing/distribution."""
import argparse
from pathlib import Path
import plistlib
import subprocess
import sys

MAGIC = {bytes.fromhex(s) for s in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}


def is_macho(path):
    with path.open("rb") as stream:
        return stream.read(4) in MAGIC


def dependency_allowed(value):
    return value.startswith(("/System/Library/", "/usr/lib/", "@rpath/", "@loader_path/", "@executable_path/"))


def verify(app, arch, run=subprocess.check_output):
    app = Path(app).resolve()
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if info.get("CFBundleIdentifier") != "io.github.gitcanvas.GitCanvas" or info.get("CFBundleExecutable") != "GitCanvas":
        raise ValueError("Unexpected bundle identity")
    if info.get("CFBundlePackageType") != "APPL" or not info.get("LSMinimumSystemVersion"):
        raise ValueError("Missing application metadata")
    executable = app / "Contents/MacOS/GitCanvas"
    if not executable.is_file() or not is_macho(executable):
        raise ValueError("Application executable is not Mach-O")
    count = 0
    for path in app.rglob("*"):
        if path.is_symlink():
            try:
                path.resolve(strict=True).relative_to(app)
            except (ValueError, OSError) as error:
                raise ValueError(f"Broken or external bundle symlink: {path}") from error
            continue
        if not path.is_file() or not is_macho(path):
            continue
        count += 1
        architectures = run(["lipo", "-archs", str(path)], text=True).split()
        if arch not in architectures:
            raise ValueError(f"Missing {arch} architecture: {path}")
        lines = run(["otool", "-arch", arch, "-L", str(path)], text=True).splitlines()[1:]
        for line in lines:
            dependency = line.strip().rsplit(" (", 1)[0]
            if not dependency_allowed(dependency):
                raise ValueError(f"External dependency: {path}: {dependency}")
        load_commands = run(["otool", "-arch", arch, "-l", str(path)], text=True).splitlines()
        rpath = False
        for line in load_commands:
            line = line.strip()
            if line.startswith("cmd "):
                rpath = line == "cmd LC_RPATH"
            elif rpath and line.startswith("path "):
                value = line[5:].rsplit(" (offset ", 1)[0]
                if not value.startswith(("@loader_path", "@executable_path", "/System/Library/", "/usr/lib/")):
                    raise ValueError(f"External runtime search path: {path}: {value}")
                rpath = False
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("app", type=Path)
    parser.add_argument("--arch", choices=["arm64", "x86_64"], required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Bundle verification must run on macOS with lipo and otool")
    print(f"Verified {verify(args.app, args.arch)} Mach-O files for {args.arch}")
