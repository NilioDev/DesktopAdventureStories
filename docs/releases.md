# Releases

A release is made from a tag. Every package is built by
`.github/workflows/release.yml` and published as a GitHub release.

| Package | Built on | What is in it |
| --- | --- | --- |
| `DesktopAdventureStories-<version>-windows.zip` | Windows, MSVC | the program (SDL and the C++ runtime linked in), license, readme |
| `DesktopAdventureStories-<version>-macos.dmg` | macOS | the app for Apple silicon and Intel, from macOS 11 |
| `DesktopAdventureStories-<version>-linux.tar.gz` | Ubuntu 22.04 | the program (SDL linked in), license, readme |
| `DesktopAdventureStories-<version>-android.apk` | Ubuntu | the app for arm64, armv7 and x86_64, from Android 5 |

No package ever holds anything of the original games.

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(... VERSION x.y.z)`).
   The Android version is taken from there as well.
2. Merge that to `main`, then tag it: `git tag vx.y.z && git push origin vx.y.z`.
   Or, on GitHub, open Releases, *Draft a new release*, type the new tag
   `vx.y.z` (target `main`) and publish it; that makes the tag too.
3. The workflow checks that the tag matches the version, builds everything
   and creates the release with notes for each system. When the release was
   already made on GitHub, the packages are added to it.

Starting the workflow by hand (Actions, Release, Run workflow) only builds
the packages, as workflow artifacts, to try the packaging.

Desktop packages can also be made locally: build, then `cpack -C Release`
in the build folder; they land in `build/packages`.

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
