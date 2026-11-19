# Build instructions

HeartCast has two parts:
* **libobs and the engine plugins**, compiled from OBS Studio source (pinned tag `32.2.2`)
* **HeartCastStudio.exe**: this repository (C++20, Qt 6, CMake)

## Option A: GitHub Actions (recommended)

`.github/workflows/windows-build.yml` runs everything on GitHub's `windows-2025-vs2026` image,
the same image OBS uses for its own builds:

1. Builds OBS with the UI, browser, scripting and websocket turned off. Only libobs and the engine
   plugins are kept. The virtual camera is renamed to "HeartCast Virtual Camera" and given its own
   CLSID so it never clashes with an OBS install.
2. Installs Qt 6.8 (Multimedia, WebSockets) and builds HeartCast against the installed libobs package.
3. Runs the unit tests, then a best-effort engine smoke test. Hosted runners have no GPU, so a
   failure in that step is allowed.
4. Runs `tools/package_windows.ps1` to lay out the OBS-compatible folder and `windeployqt`.
5. Downloads `vc_redist.x64.exe`, compiles `installer/HeartCast.iss` with Inno Setup and zips a
   portable build.
6. On `v*` tags, publishes a GitHub Release plus `update.json`. The app's updater feed is set to
   `https://github.com/<you>/<repo>/releases/latest/download/update.json`.

## Option B: build by hand on Windows 10/11 (x64)

Prerequisites: Visual Studio 2022 or 2026 with "Desktop development with C++", CMake 3.28+,
Git, Qt 6.8 MSVC x64 (Multimedia, WebSockets), and Inno Setup 6.3+.

```powershell
# 1) engine
git clone --recursive --branch 32.2.2 https://github.com/obsproject/obs-studio.git
cd obs-studio
cmake --preset windows-x64 -DENABLE_FRONTEND=OFF -DENABLE_BROWSER=OFF -DENABLE_SCRIPTING=OFF `
      -DENABLE_WEBSOCKET=OFF -DVIRTUALCAM_GUID=B7A3E4D1-5C2F-4A8E-9D61-3F0C2E8B7A54
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix C:\obs-dist
cd ..

# 2) app (use -G "Visual Studio 17 2022" if you have VS 2022)
cmake -S HeartCast -B hcbuild -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64;C:\obs-dist" `
      -Dlibobs_DIR=C:\obs-dist\cmake\libobs -Dw32-pthreads_DIR=C:\obs-dist\cmake\w32-pthreads
cmake --build hcbuild --config Release

# 3) package + installer
cd HeartCast
.\tools\package_windows.ps1 -ObsInstall C:\obs-dist -AppExe ..\hcbuild\bin\Release\HeartCastStudio.exe `
      -QtBin C:\Qt\6.8.3\msvc2022_64\bin -Out ..\dist\HeartCast -VcRedist C:\path\vc_redist.x64.exe
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DSourceDir=..\dist\HeartCast /DAppVersion=0.1.0 installer\HeartCast.iss
```
If `libobsConfig.cmake` is somewhere else in your OBS install, run
`dir C:\obs-dist -r -fi libobsConfig.cmake` and point `libobs_DIR` at that folder.

The installed layout mirrors OBS, so libobs finds its shaders and plugins automatically:
`bin\64bit\HeartCastStudio.exe`, `obs-plugins\64bit\*.dll`, `data\libobs`, `data\obs-plugins\<module>`.

## Option C: Linux development build

The UI and the engine also build on Linux against the distro's libobs. This is handy for
fast iteration and runs the same integration tests:
```bash
sudo apt install libobs-dev obs-studio qt6-base-dev qt6-multimedia-dev qt6-websockets-dev \
                 libqt6svg6-dev libgl1-mesa-dev xvfb ffmpeg ninja-build
cmake -S . -B build -G Ninja && cmake --build build
./build/tests/heartcast_unit_tests -platform offscreen
./tests/run_engine_test.sh build      # real recording, replay buffer, 2-server multistream
./build/HeartCastStudio
```

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
5. **Microsoft Entra ID → App registrations → New registration** (e.g. `heartcast-signing`).
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
`#define`s at the top of `installer/HeartCast.iss`. Regenerate the icons with
`tools/make_assets.sh` after editing `resources/icons/logo.svg`.

## Licensing notes
libobs is GPL-2.0-or-later, so HeartCast is distributed under the GPL. Ship the source (or a written
offer) with binaries. The OBS build is configured without FDK-AAC, and x264 is GPL, which is
compatible. Qt is used under the LGPL through dynamic linking. See THIRD-PARTY-LICENSES.md.
