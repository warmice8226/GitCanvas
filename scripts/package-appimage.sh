#!/usr/bin/env bash
set -euo pipefail
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'Linux x86_64 build host required' >&2; exit 1; }
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="$project_root/build-linux"
package_root="$project_root/out/linux"
mkdir -p "$package_root"
tool_dir="$build_root/linux-tools"
python3 "$project_root/scripts/prepare-linux-tools.py" "$tool_dir"
cmake -S "$project_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build_root" --parallel "${BUILD_JOBS:-2}"
if ! ctest --test-dir "$build_root" --output-on-failure; then
    tail -n 100 "$build_root/prototype-tests.txt" || true
    exit 1
fi
appdir="$(mktemp -d "$package_root/GitCanvas.AppDir.XXXXXX")"
DESTDIR="$appdir" cmake --install "$build_root"
license_dir="$appdir/usr/share/doc/gitcanvas/third-party-licenses"
mkdir -p "$license_dir"
cp "$(command -v gh)" "$appdir/usr/bin/gh"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QMAKE:-/usr/bin/qmake6}"
export EXTRA_QT_PLUGINS=platforms
export PATH="$tool_dir:$PATH"
export OUTPUT="$package_root/GitCanvas-0.1.0-linux-x86_64.AppImage"
"$tool_dir/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --executable "$appdir/usr/bin/GitCanvas" --desktop-file "$project_root/packaging/io.github.gitcanvas.GitCanvas.desktop" --icon-file "$project_root/assets/io.github.gitcanvas.GitCanvas.png" --plugin qt
# Keep installed package versions and their notices for release source matching.
dpkg-query -W > "$appdir/usr/share/doc/gitcanvas/build-packages.txt"
find /usr/share/doc -name copyright -type f -print0 | while IFS= read -r -d '' notice; do
    cp "$notice" "$license_dir/$(basename "$(dirname "$notice")")-copyright"
done
python3 "$project_root/scripts/portable-metadata.py" --package "$appdir" --sources "$package_root/GitCanvas-0.1.0-source.zip"
"$tool_dir/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --output appimage
QT_QPA_PLATFORM=offscreen "$OUTPUT" --smoke-test
(cd "$package_root" && sha256sum ./*.AppImage ./*-source.zip > SHA256SUMS)
echo "Portable AppImage: $OUTPUT"
