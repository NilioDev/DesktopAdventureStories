#!/usr/bin/env bash
# Makes DesktopAdventureStories-<version>-linux-x86_64.AppImage: one file that
# starts with a double click on almost any Linux system, nothing to install.
#
#   packaging/linux/make-appimage.sh <build folder> <output folder>
#
# The build folder holds a Release build with SDL linked in
# (-DDAS_USE_SYSTEM_SDL3=OFF). appimagetool is downloaded once, pinned by
# its checksum; set APPIMAGETOOL to use another copy.
set -euo pipefail

build=$(realpath "$1")
out=$(realpath -m "$2")
here=$(dirname "$(realpath "$0")")
root=$(realpath "$here/../..")
id=org.desktopadventurestories.app

tool_version=1.9.0
tool_sha256=46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1

version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)
arch=$(uname -m)
program="$build/src/app/DesktopAdventureStories"
[ -x "$program" ] || { echo "No program at $program; build first" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -z "${APPIMAGETOOL:-}" ]; then
    APPIMAGETOOL="$work/appimagetool"
    curl -fsSL -o "$APPIMAGETOOL" \
        "https://github.com/AppImage/appimagetool/releases/download/$tool_version/appimagetool-$arch.AppImage"
    if [ "$arch" = x86_64 ]; then
        echo "$tool_sha256  $APPIMAGETOOL" | sha256sum -c -
    fi
    chmod +x "$APPIMAGETOOL"
fi

# The folder the AppImage is made of, laid out as the AppImage spec wants.
app="$work/AppDir"
install -Dm755 "$program" "$app/usr/bin/DesktopAdventureStories"
strip "$app/usr/bin/DesktopAdventureStories"
install -Dm644 "$here/$id.desktop" "$app/usr/share/applications/$id.desktop"
install -Dm644 "$here/$id.metainfo.xml" "$app/usr/share/metainfo/$id.appdata.xml"
install -Dm644 "$here/$id.png" "$app/usr/share/icons/hicolor/256x256/apps/$id.png"
install -Dm644 "$here/$id.svg" "$app/usr/share/icons/hicolor/scalable/apps/$id.svg"
install -Dm644 "$root/LICENSE" "$app/usr/share/doc/DesktopAdventureStories/LICENSE"
ln -s "usr/share/applications/$id.desktop" "$app/$id.desktop"
ln -s "usr/share/icons/hicolor/256x256/apps/$id.png" "$app/$id.png"
ln -s "$id.png" "$app/.DirIcon"
ln -s usr/bin/DesktopAdventureStories "$app/AppRun"

mkdir -p "$out"
# Runs without FUSE, so it also works in containers and on build machines.
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" VERSION="$version" \
    "$APPIMAGETOOL" --no-appstream "$app" "$out/DesktopAdventureStories-$version-linux-$arch.AppImage"
echo "Made $out/DesktopAdventureStories-$version-linux-$arch.AppImage"
