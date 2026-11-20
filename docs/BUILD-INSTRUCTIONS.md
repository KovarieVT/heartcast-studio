# Build instructions

KovaLive has three parts:
* **libobs and the engine plugins** (including obs-vst for VST 2.x), compiled from OBS Studio
  source (pinned tag `32.2.2`)
* **KovaLive.exe**: this repository (C++20, Qt 6, CMake)
* **obs-vst3** (VST3 host, `third_party/obs-vst3`), built with the app against the Steinberg VST3
  SDK v3.8.0

**Qt version must match OBS's.** obs-vst is built by OBS against the Qt in OBS's prebuilt deps
(obs-deps 2026-07-15 → **Qt 6.11.1** for OBS 32.2.2) and loads that Qt's DLLs from KovaLive's
folder. With a different Qt version it fails to load, so the app must be built with the same
Qt. The CI checks this and stops with a clear message if they differ.

## Option A: GitHub Actions (recommended)

`.github/workflows/windows-build.yml` runs everything on GitHub's `windows-2025-vs2026` image,
the same image OBS uses for its own builds:

1. Builds OBS with the UI, browser, scripting and websocket turned off. Only libobs and the engine
   plugins are kept. The virtual camera is renamed to "KovaLive Virtual Camera" and given its own
   CLSID so it never clashes with an OBS install.
2. Checks the Qt version OBS used, installs the same Qt (6.11.1, with Multimedia, WebSockets,
   Image Formats), clones the Steinberg VST3 SDK (tag `v3.8.0_build_66`, commit verified) and
   builds KovaLive plus obs-vst3 against the installed libobs package.
3. Runs the unit tests.
4. Runs `tools/package_windows.ps1` to lay out the OBS-compatible folder, adds obs-vst3 and its
   scanner, and runs `windeployqt` for the app and both VST hosts.
5. Runs the engine test on the packaged build (Direct3D 11 on Microsoft's software GPU, full
   page-heap): recording, replay buffer, scenes, all 26 stingers, the gothic wipes, all 89
   widgets, and that both VST hosts load.
6. Downloads `vc_redist.x64.exe`, compiles `installer/KovaLive.iss` with Inno Setup 6.6+ and zips
   a portable build.
7. On `v*` tags, publishes a GitHub Release plus `update.json`. The app's updater feed is set to
   `https://github.com/<you>/<repo>/releases/latest/download/update.json`.

## Option B: build by hand on Windows 10/11 (x64)

Prerequisites: Visual Studio 2022 or 2026 with "Desktop development with C++", CMake 3.28+,
Git, Qt **6.11.1** MSVC x64 (Multimedia, WebSockets, Image Formats), and Inno Setup 6.6+.

```powershell
# 1) engine
git clone --recursive --branch 32.2.2 https://github.com/obsproject/obs-studio.git
cd obs-studio
cmake --preset windows-x64 -DENABLE_FRONTEND=OFF -DENABLE_BROWSER=OFF -DENABLE_SCRIPTING=OFF `
      -DENABLE_WEBSOCKET=OFF -DVIRTUALCAM_GUID=B7A3E4D1-5C2F-4A8E-9D61-3F0C2E8B7A54
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix C:\obs-dist
cd ..

# 2) VST3 SDK (MIT) for the VST3 host
git clone --depth 1 --branch v3.8.0_build_66 https://github.com/steinbergmedia/vst3sdk C:\vst3\vst3sdk
git -C C:\vst3\vst3sdk submodule update --init --depth 1 base pluginterfaces public.sdk

# 3) app + obs-vst3 (use -G "Visual Studio 17 2022" if you have VS 2022)
cmake -S KovaLive -B hcbuild -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64;C:\obs-dist" `
      -Dlibobs_DIR=C:\obs-dist\cmake\libobs -Dw32-pthreads_DIR=C:\obs-dist\cmake\w32-pthreads `
      -DKOVALIVE_BUILD_VST3=ON -DSTEINBERG_VST3_PATH=C:\vst3
cmake --build hcbuild --config Release
cmake --install hcbuild --config Release --prefix C:\app-stage

# 4) package + installer
cd KovaLive
.\tools\package_windows.ps1 -ObsInstall C:\obs-dist -AppExe ..\hcbuild\bin\Release\KovaLive.exe `
      -AppStage C:\app-stage -QtBin C:\Qt\6.11.1\msvc2022_64\bin -Out ..\dist\KovaLive `
      -VcRedist C:\path\vc_redist.x64.exe
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DSourceDir=..\dist\KovaLive /DAppVersion=0.2.0 installer\KovaLive.iss
```
If `libobsConfig.cmake` is somewhere else in your OBS install, run
`dir C:\obs-dist -r -fi libobsConfig.cmake` and point `libobs_DIR` at that folder.

The installed layout mirrors OBS, so libobs finds its shaders and plugins automatically:
`bin\64bit\KovaLive.exe`, `obs-plugins\64bit\*.dll`, `data\libobs`, `data\obs-plugins\<module>`.

## Option C: Linux development build

The UI and the engine also build on Linux against the distro's libobs. This is handy for
fast iteration and runs the same integration tests:
```bash
sudo apt install libobs-dev obs-studio qt6-base-dev qt6-multimedia-dev qt6-websockets-dev \
                 libqt6svg6-dev libgl1-mesa-dev xvfb ffmpeg ninja-build
cmake -S . -B build -G Ninja && cmake --build build
./build/tests/kovalive_unit_tests -platform offscreen
./tests/run_engine_test.sh build      # real recording, replay buffer, 2-server multistream,
                                      # stingers, wipes and every widget
./build/KovaLive
./build/KovaLive --open settings:Stream   # open a window at start-up (also: widgets[:Category],
                                          # transitions, media, effects, recordings, analytics, plugins)
```
The VST3 host is a Windows deliverable (`-DKOVALIVE_BUILD_VST3=OFF` by default elsewhere).

## Regenerating the art (logo, installer images, stingers)
Everything is drawn by code. With Qt (incl. Svg), ImageMagick and an FFmpeg with libvpx:
```bash
cmake -S . -B build -G Ninja -DKOVALIVE_BUILD_ART_TOOL=ON && cmake --build build --target render_art
tools/make_assets.sh build      # icons, app.ico, wizard BMPs, 26 stingers (VP9 + alpha WebM)
```
`render_art` measures every stinger frame and fails if a stinger doesn't fully cover the screen
for at least three frames; the cut point it writes to `resources/stingers/stingers.json` is the
middle of the longest fully covered stretch.

## Code signing

Builds are unsigned until you set this up. Without a signature, Windows SmartScreen warns
"unknown publisher", and the in-app updater refuses to auto-run unsigned installers.

Since June 2023, code-signing keys must live in hardware (a USB token or a cloud HSM), so
certificates can no longer be downloaded as a `.pfx` file. The workflow uses **Azure Artifact
Signing** (formerly Trusted Signing): about $9.99/month, keys held by Microsoft, and it signs
directly inside GitHub Actions. Individual developers are supported in the USA and Canada;
organizations also in the EU and UK.

1. Create an Azure account and subscription (portal.azure.com).
2. Create an **Artifact Signing account**. Note its **Account URI**
   (e.g. `https://eus.codesigning.azure.net/`) and **account name**.
3. **Identity validation → New identity → Public → Individual**. Verify with a government ID
   through Microsoft Entra Verified ID. This can take hours to a few days.
4. **Certificate profiles → Add → Public Trust**, using the validated identity. Note the profile name.
5. **Microsoft Entra ID → App registrations → New registration** (e.g. `kovalive-signing`).
   Create a **client secret**, and note the tenant ID, client ID and secret.
6. On the certificate profile, open **Access control (IAM)** and give the app registration the
   **Artifact Signing Certificate Profile Signer** role.
7. In your GitHub repository, open **Settings → Secrets and variables → Actions** and add:
   `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`, `ARTIFACT_SIGNING_ENDPOINT`
   (the Account URI), `ARTIFACT_SIGNING_ACCOUNT`, `ARTIFACT_SIGNING_PROFILE`.

The next build signs every `.exe` and `.dll` in the package plus the installer, with a timestamp.
All of them must be signed: Windows 11 **Smart App Control** blocks an app if *any* binary it loads
(e.g. an FFmpeg or libobs DLL) is unsigned, and it allows no per-app exceptions. To check, right-click
the file → Properties → **Digital Signatures**.

Free alternative for open-source projects: **SignPath Foundation** (signpath.org) signs GPL projects
at no cost after review. The publisher shown is "SignPath Foundation", not your name, and your
releases must credit them.

SmartScreen reputation still builds up over time, even for signed apps. Early downloads may show a
softer warning until enough people have installed it.

## Renaming the product
`APP_NAME`, `APP_EXE`, `APP_ORG` and `APP_VERSION` at the top of `CMakeLists.txt`, plus the
`#define`s at the top of `installer/KovaLive.iss`. A rename that should not upgrade the old
install in place needs a new `AppId` (see how `LegacyAppId` is handled in the installer).
Regenerate the art with `tools/make_assets.sh`.

## Licensing notes
libobs is GPL-2.0-or-later, so KovaLive is distributed under the GPL. obs-vst3 is
GPL-3.0-or-later, so the Windows package as a whole is GPL-3.0. Ship the source (or a written
offer) with binaries. The OBS build is configured without FDK-AAC, and x264 is GPL, which is
compatible. Qt is used under the LGPL through dynamic linking. See THIRD-PARTY-LICENSES.md.
