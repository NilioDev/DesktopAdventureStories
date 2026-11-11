# Releases

A release is made from a tag. Every package is built by
`.github/workflows/release.yml` and published as a GitHub release.

| Package | Built on | What is in it |
| --- | --- | --- |
| `DesktopAdventureStories-<version>-windows.zip` | Windows, MSVC | the program (SDL and the C++ runtime linked in), license, readme |
| `DesktopAdventureStories-<version>-macos.dmg` | macOS | the app for Apple silicon and Intel, from macOS 11 |
| `DesktopAdventureStories-<version>-linux-x86_64.AppImage` | Ubuntu 22.04 | the same program as one file that starts with a double click, with its menu entry and icon |
| `DesktopAdventureStories-<version>-linux.tar.gz` | Ubuntu 22.04 | the program (SDL linked in), license, readme |
| `DesktopAdventureStories-<version>-android.apk` | Ubuntu | the app for arm64, armv7 and x86_64, from Android 5 |

No package ever holds anything of the original games.

## Making a release

1. On GitHub, open Releases, *Draft a new release*, type the new tag
   `vx.y.z` (target `main`) and publish it; that makes the tag too. Or tag
   it yourself: `git tag vx.y.z && git push origin vx.y.z`.
2. The workflow takes the version from the tag, builds everything and adds
   the packages to the release (or creates the release with notes for each
   system). A release left without a title gets the tag as title.

The version in `CMakeLists.txt` is only used for builds that are not a
release; raise it after a release, so that development builds say so.

If a release has no packages because the workflow failed, fix the cause on
`main`, then open Actions, Release, *Run workflow* and type the release's tag:
the tag is built again and the packages are added to its release.

Starting the workflow by hand without a tag only builds
the packages, as workflow artifacts, to try the packaging.

Desktop packages can also be made locally: build, then `cpack -C Release`
in the build folder; they land in `build/packages`.

## The Linux AppImage

`packaging/linux/make-appimage.sh <build folder> <output folder>` turns a
Release build into an AppImage. It downloads
[appimagetool](https://github.com/AppImage/appimagetool) (pinned version,
checked against its SHA-256) and needs `desktop-file-validate` from
`desktop-file-utils`. The menu entry, icon and AppStream data it puts in
are in `packaging/linux/`; they can be reused for a Flatpak later.

Started from an AppImage, the program also looks for a `gamedata` folder
next to the AppImage file (the `APPIMAGE` environment variable), since the
program itself runs from a read-only mount.

## Signing the Android app

Android only installs an update when it is signed with the same key as the
installed app. The release workflow signs with the key in these repository
secrets:

| Secret | Value |
| --- | --- |
| `DAS_KEYSTORE_BASE64` | the keystore file, base64 encoded |
| `DAS_KEYSTORE_PASSWORD` | the keystore's password |
| `DAS_KEY_ALIAS` | the key's name in the keystore |
| `DAS_KEY_PASSWORD` | the key's password |

A keystore can be made with
`keytool -genkeypair -v -keystore release.keystore -alias das -keyalg RSA -keysize 4096 -validity 10000`
and encoded with `base64 -w0 release.keystore`. Keep it safe: a lost key
means users have to uninstall before they can update.

Without the secrets the APK is signed with a throwaway debug key, and the
workflow says so in a warning.

## F-Droid

F-Droid builds apps from source itself. The store texts are in
`fastlane/metadata/android/en-US`, where F-Droid looks for them. Getting in
needs a merge request to F-Droid's data repository with a build recipe for
the Android project in `android/`.

## macOS

The app is not signed or notarized (that needs a paid Apple developer
account). The first time, macOS asks to confirm: right-click the app and
choose Open.
