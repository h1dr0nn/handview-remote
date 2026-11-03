# Packaging

Release tooling for the Godot addon zip. Python 3, standard library only. Nothing here builds code; it packs files that CI already built.

## package.py

```bash
python tools/package/package.py \
  --dll native/build/.../handview.windows.template_release.x86_64.dll \
  --macos-dylib godot-addon/addons/handview/bin/handview.macos.template_release.arm64.dylib \
  --apk android-app/app/build/outputs/apk/release/app-release.apk \
  --version 0.1.0 --version-code 1 --out dist
```

Writes `dist/handview-godot-addon-<version>.zip` and `dist/handview-godot-addon-<version>.zip.sha256` (`sha256sum -c` format).

Zip contents:

- `addons/handview/**` from `godot-addon`, including `.uid` files. Excluded: `bin/*` (the DLLs come from the arguments), `*.pdb`, `.godot`, `.gitkeep`, temp files, and any APK or `handview.version` in the source tree. `godot-addon/tests` and the dev project files are outside `addons/handview` and never included.
- The release DLL as `bin/handview.windows.template_release.x86_64.dll`, and the same file again as `bin/handview.windows.template_debug.x86_64.dll`. The editor loads the debug entry, so users get the optimized build either way. Pass `--debug-dll` to ship a real debug build under the debug name instead.
- With `--macos-dylib`, the macOS arm64 release build as `bin/handview.macos.template_release.arm64.dylib` and again as `bin/handview.macos.template_debug.arm64.dylib`. Without it the zip has no macOS library.
- The APK as `android/handview.apk`.
- `android/handview.version` holding the version code, which the addon compares with the version installed on the phone.

The script refuses a version that is not semver, a version code below 1, a DLL that is not a PE file, a dylib that is not a 64-bit Mach-O file, a missing APK, and a `plugin.cfg` version that differs from `--version`. The zip is reproducible: same inputs give the same bytes.

Run `python tools/package/package.py --self-test` to build a zip from dummy files and check the layout.

## changelog_section.py

`python tools/package/changelog_section.py 0.1.0` prints the `## [0.1.0]` section of `CHANGELOG.md`. The release workflow uses it as the GitHub Release body and fails when the section is missing or empty.

## Releasing

1. Update `handview.versionCode` and `handview.versionName` in `android-app/gradle.properties`, `version` in `godot-addon/addons/handview/plugin.cfg`, and move the Unreleased notes under a new `## [x.y.z]` heading in `CHANGELOG.md`.
2. Commit, then tag and push: `git tag vx.y.z && git push origin vx.y.z`.
3. `.github/workflows/release.yml` checks the tag against those files, builds the Release DLL, the macOS arm64 dylib and the signed APK, packages the zip and publishes the GitHub Release with the zip, its checksum and the standalone APK.

### Secrets

Create these under Settings > Secrets and variables > Actions:

| Secret | Content |
|---|---|
| `HANDVIEW_KEYSTORE_BASE64` | The release keystore, base64 encoded (`base64 -w0 handview-release.jks`) |
| `HANDVIEW_KEYSTORE_PASSWORD` | Keystore password |
| `HANDVIEW_KEY_ALIAS` | Key alias inside the keystore |
| `HANDVIEW_KEY_PASSWORD` | Key password |

Local release builds read the same names from the environment, with `HANDVIEW_KEYSTORE_FILE` pointing at the keystore file. Without all four variables `./gradlew :app:assembleRelease` produces an unsigned APK. Never commit a keystore; `*.jks` and `*.keystore` are gitignored. Keep the keystore backed up: phones refuse to update an app signed with a different key.
