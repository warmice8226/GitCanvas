#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'macOS with Xcode Command Line Tools is required; Windows cannot build this app bundle.' >&2
    exit 1
fi
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
arch="$(uname -m)"
case "$arch" in arm64|x86_64) ;; *) echo "Unsupported architecture: $arch" >&2; exit 1;; esac
qt_root="${QT_ROOT:-$(brew --prefix qtbase)}"
export PATH="$qt_root/bin:$PATH"
deploy_tool="${MACDEPLOYQT:-$qt_root/bin/macdeployqt}"
if [[ ! -x "$deploy_tool" ]] && command -v brew >/dev/null; then deploy_tool="$(brew --prefix qttools)/bin/macdeployqt"; fi
build_root="$project_root/build-macos-$arch"
package_root="$project_root/out/macos-$arch"
identity="${CODESIGN_IDENTITY:--}"
profile="${NOTARY_PROFILE:-}"
if [[ -n "$profile" && "$identity" != 'Developer ID Application: '* ]]; then
    echo 'NOTARY_PROFILE requires a Developer ID Application signing identity.' >&2; exit 1
fi
for tool in cmake ninja python3 git ditto codesign lipo otool sips iconutil; do command -v "$tool" >/dev/null; done
[[ -x "$deploy_tool" ]]
mkdir -p "$package_root"
# Each run gets its own staging directory; existing artifacts or user paths are not removed.
stage_root="$(mktemp -d "$package_root/stage.XXXXXX")"
cmake -S "$project_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt_root" -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-15.0}"
cmake --build "$build_root" --parallel "${BUILD_JOBS:-2}"
if ! ctest --test-dir "$build_root" --output-on-failure; then
    tail -n 100 "$build_root/prototype-tests.txt" || true
    exit 1
fi
cmake --install "$build_root" --prefix "$stage_root"
app="$stage_root/GitCanvas.app"
[[ -x "$app/Contents/MacOS/GitCanvas" ]]
mkdir -p "$app/Contents/Resources/docs" "$app/Contents/Resources/licenses" "$app/Contents/PlugIns/platforms"
cp "$project_root"/docs/*.md "$project_root"/docs/*.html "$app/Contents/Resources/docs/"
cp "$project_root/LICENSE" "$app/Contents/Resources/licenses/GitCanvas-LICENSE"
cp -R "$project_root/licenses" "$app/Contents/Resources/licenses/Git-docs"
cp "$project_root/THIRD_PARTY_NOTICES.md" "$app/Contents/Resources/licenses/"
iconset="$stage_root/GitCanvas.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$project_root/assets/gitcanvas-scroll.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$project_root/assets/gitcanvas-scroll.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/GitCanvas.icns"
/usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string GitCanvas.icns' "$app/Contents/Info.plist"
# Include the matching SDK/source notices; never silently produce a package without them.
license_root="${QT_LICENSE_DIR:-$qt_root}"
python3 - "$license_root" "$app/Contents/Resources/licenses/Qt" <<'PY'
from pathlib import Path
import shutil, sys
source, target = map(Path, sys.argv[1:])
count = 0
for path in source.rglob('*'):
    if path.is_file() and (any(word in path.name.upper() for word in ('LICENSE', 'COPYING', 'NOTICE')) or 'LICENSES' in path.parts):
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        count += 1
if not count:
    raise SystemExit('Qt notices missing: set QT_LICENSE_DIR to notices/source for this SDK version')
PY
plugin_root="$("$qt_root/bin/qmake" -query QT_INSTALL_PLUGINS)"
cp "$plugin_root/platforms/libqoffscreen.dylib" "$app/Contents/PlugIns/platforms/"
deploy_args=("$app" '-always-overwrite')
# Optional locally prepared gh: copy only its executable and license, never credentials/config.
if [[ -n "${GITHUB_CLI_BINARY:-}" ]]; then
    [[ -x "$GITHUB_CLI_BINARY" && -f "${GITHUB_CLI_LICENSE:-}" ]]
    cp "$GITHUB_CLI_BINARY" "$app/Contents/MacOS/gh"
    cp "$GITHUB_CLI_LICENSE" "$app/Contents/Resources/licenses/GitHub-CLI-LICENSE"
    deploy_args+=("-executable=$app/Contents/MacOS/gh")
fi
if [[ "$identity" == '-' ]]; then deploy_args+=('-codesign=-'); else deploy_args+=("-sign-for-notarization=$identity"); fi
"$deploy_tool" "${deploy_args[@]}"
python3 "$project_root/scripts/verify-macos-bundle.py" "$app" --arch "$arch"
codesign --verify --deep --strict --verbose=2 "$app"
# Start the packaged executable without build-time Qt paths or Homebrew libraries in the environment.
python3 - "$app/Contents/MacOS/GitCanvas" <<'PY'
import os, subprocess, sys
env = {k:v for k,v in os.environ.items() if not k.startswith(('DYLD_', 'QT_'))}
env['PATH'] = '/usr/bin:/bin:/usr/sbin:/sbin'
subprocess.run([sys.argv[1], '--smoke-test', '-platform', 'offscreen'], env=env, check=True, timeout=30)
PY
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
base="$package_root/GitCanvas-$version-macos-$arch"
if [[ -n "$profile" ]]; then
    ditto -c -k --sequesterRsrc --keepParent "$app" "$stage_root/notary-input.zip"
    xcrun notarytool submit "$stage_root/notary-input.zip" --keychain-profile "$profile" --wait --timeout 20m --output-format json > "$package_root/notary-app.json"
    python3 -c 'import json,sys; assert json.load(open(sys.argv[1]))["status"] == "Accepted"' "$package_root/notary-app.json"
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    spctl --assess --type execute --verbose=2 "$app"
fi
python3 "$project_root/scripts/portable-metadata.py" --package "$app" --sources "$package_root/GitCanvas-0.1.0-source.zip" --inventory-output "$package_root/runtime-inventory.json"
ditto -c -k --sequesterRsrc --keepParent "$app" "$base.zip"
{
    sw_vers
    cmake --version
    git --version
    "$qt_root/bin/qmake" -query QT_VERSION
    printf 'Architecture: %s\nSigning: %s\nNotarization requested: %s\n' "$arch" "$identity" "${profile:+yes}"
} > "$package_root/build-environment.txt"
(cd "$package_root" && shasum -a 256 "$(basename "$base.zip")" GitCanvas-0.1.0-source.zip > SHA256SUMS)
echo "macOS packages: $base.zip"
