# Signing and distribution

Build and validate the Release configuration on every target platform. Replace
the example names, identifiers, version, and certificate names with your product's
identity. Freeze plugin codes, parameter IDs, parameter hints, and product type
before release. CI builds and tests; it does not publish or distribution-sign code.

## macOS

Use a fresh universal build if supporting Intel and Apple Silicon. Choose your
minimum OS explicitly (11.0 below is an example product policy):

```sh
cmake -S . -B build/distribution -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build/distribution --config Release --parallel 6
```

For an instrument add `-DBOILERPLATE_IS_SYNTH=ON` and your instrument's identity
overrides, as in the instrument presets. The paths below assume the default effect.

### Sign the bundles

Sign nested frameworks, helper executables, or bundled libraries **inside out**
before signing their containing bundle if your product adds any. The current
boilerplate does not bundle those dependencies. Do not use `--deep` as a substitute
for signing each nested code item with its appropriate entitlements.

```sh
artefacts="build/distribution/JUCEBoilerplate_artefacts/Release"
identity="Developer ID Application: Your Name (TEAMID)"

codesign --force --timestamp --options runtime --sign "$identity" \
  "$artefacts/AU/JUCE Boilerplate.component"
codesign --force --timestamp --options runtime --sign "$identity" \
  "$artefacts/VST3/JUCE Boilerplate.vst3"
codesign --force --timestamp --options runtime --sign "$identity" \
  --entitlements packaging/macos/Standalone.entitlements \
  "$artefacts/Standalone/JUCE Boilerplate.app"

codesign --verify --deep --strict --verbose=2 "$artefacts/AU/JUCE Boilerplate.component"
codesign --verify --deep --strict --verbose=2 "$artefacts/VST3/JUCE Boilerplate.vst3"
codesign --verify --deep --strict --verbose=2 "$artefacts/Standalone/JUCE Boilerplate.app"
```

The standalone entitlement permits audio input under hardened runtime. JUCE's
generated effect standalone Info.plist supplies the microphone usage description; adjust
that text for the product. Plugin bundles run inside a host and do not receive
the standalone's entitlements. Add other entitlements only when the product needs
them. Sign after building: rebuilding or regenerating VST3 metadata changes the bundle.

### Package, notarize, and staple

Use a fresh staging directory for each release. The following creates a signed
installer for `/Applications` and the system plugin directories. Installing this
package requests administrator privileges. Creating it does not install anything.

```sh
staging="$(mktemp -d /tmp/juce-release.XXXXXX)"
mkdir -p "$staging/Applications" \
  "$staging/Library/Audio/Plug-Ins/Components" \
  "$staging/Library/Audio/Plug-Ins/VST3"
ditto "$artefacts/Standalone/JUCE Boilerplate.app" "$staging/Applications/JUCE Boilerplate.app"
ditto "$artefacts/AU/JUCE Boilerplate.component" "$staging/Library/Audio/Plug-Ins/Components/JUCE Boilerplate.component"
ditto "$artefacts/VST3/JUCE Boilerplate.vst3" "$staging/Library/Audio/Plug-Ins/VST3/JUCE Boilerplate.vst3"
mkdir -p build/packages
pkgbuild --root "$staging" --install-location / \
  --identifier com.boilerplateaudio.juceboilerplate.pkg --version 0.1.0 \
  --sign "Developer ID Installer: Your Name (TEAMID)" \
  "build/packages/JUCE Boilerplate.pkg"
```

Store notarization credentials in a Keychain profile using
`xcrun notarytool store-credentials` once; use your Apple Developer account or
App Store Connect API credentials. Keep credentials out of the repository.

```sh
xcrun notarytool submit "build/packages/JUCE Boilerplate.pkg" \
  --keychain-profile "audio-notary" --wait
# Continue only after the submission reports Accepted.
xcrun stapler staple "build/packages/JUCE Boilerplate.pkg"
xcrun stapler validate "build/packages/JUCE Boilerplate.pkg"
pkgutil --check-signature "build/packages/JUCE Boilerplate.pkg"
spctl --assess --type install --verbose=2 "build/packages/JUCE Boilerplate.pkg"
```

For a rejected submission, inspect `xcrun notarytool log <submission-id>` using
the same credential profile and correct the reported signing issue. Distribute
the accepted, stapled package. Test it on a clean machine, including a downloaded
copy subject to Gatekeeper. If distributing an app separately instead, notarize
its ZIP archive, staple the app itself after acceptance, then recreate the ZIP.
Do not try to staple a ZIP file or a standalone VST3/component bundle.

Reference: Apple's [notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
and [custom notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).

## Windows and Linux

- Windows: build x64 with Visual Studio 2022; package the `.vst3` bundle and
  standalone `.exe`. Choose and test an MSVC runtime deployment strategy. Sign
  shipped executables/plugin binaries and the installer with your release
  certificate and timestamp service. Test installation, host scanning, and
  uninstall behavior as a standard user on supported Windows versions.
- Linux: build on the oldest distribution/libc baseline you intend to support.
  Package the entire `.vst3` directory and standalone executable, preserving
  executable permissions. Install VST3 to `~/.vst3` for a user install. Check
  runtime library dependencies with `ldd` and test on a clean target system.

## Release validation

1. Build all formats and run CTest for effect/instrument, Debug/Release. Review
   cross-platform CI results. Test both slices of any universal macOS binary.
2. Install and run `auval -v aufx Jbpl Wckm` for the default effect, or
   `auval -v aumu Jbpi Wckm` for the instrument preset. Use your released codes
   for rebranded products. Validate the final signed bundles too.
3. Run [pluginval](https://github.com/Tracktion/pluginval) at strictness 5 or
   higher on each shipped VST3 and AU. Use a version supporting the target
   architecture. Example commands for the default Release build:

   ```sh
   pluginval --strictness-level 5 "build/release/JUCEBoilerplate_artefacts/Release/VST3/JUCE Boilerplate.vst3"
   # macOS, if pluginval is installed as an application:
   /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 \
     "build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component"
   ```

   On headless Linux prefix the command with `xvfb-run -a`; on Windows use
   `pluginval.exe`. Require a zero exit status and retain the validation log.
4. In actual target DAWs, scan/load both formats; automate gain and bypass;
   save, close, and reopen a session; resize and repeatedly reopen the editor;
   change sample rates/buffer sizes; compare offline export with realtime audio.
   Include Logic for AU and a VST3 host on each supported OS.
5. Launch standalone on each OS. Select audio devices, confirm microphone
   permission/input for the effect, MIDI input for instruments, resizing, and
   clean shutdown. The instrument scaffold is silent until an engine is added;
   validate note timing and voice limits once that engine exists.
6. Test silence, gain extremes, mono/stereo, rapid bypass changes, and many
   instances at small buffer sizes. Profile optimized builds before adding more
   DSP optimizations. Confirm tails/latency reporting when extending the DSP.
7. Install the final package on the oldest supported clean OS and verify signing,
   recall from the previous release, and uninstall behavior before publishing.
