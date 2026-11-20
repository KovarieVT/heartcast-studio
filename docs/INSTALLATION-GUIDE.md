# Installation guide

## Requirements
* Windows 10 version 1809 or newer, or Windows 11, 64-bit
* A DirectX 11 GPU with an up-to-date driver
* About 400 MB of disk space, plus space for recordings
* For GPU encoding: NVIDIA GTX 10-series or newer, AMD RX 400 or newer, or Intel 6th-gen or newer

## Install
1. Run `KovaLive-<version>-Setup.exe`.
   If the build isn't code-signed, SmartScreen will say "Windows protected your PC".
   Choose *More info → Run anyway* only if you built it yourself or trust where it came from.
2. Choose **Install for all users** (needed for the virtual camera) or **Install for me only**
   (no administrator rights).
3. **Welcome** → **License** (GPL; the Windows package is GPL-3.0, see THIRD-PARTY-LICENSES) →
   **Location** (default `C:\Program Files\KovaLive`)
4. **Components:**
   * *Virtual Camera*: registers "KovaLive Virtual Camera" so Discord, Zoom, Teams and Meet can see it
   * *Gothic overlay packs, starter scenes & stinger transitions*, *Documentation*
5. **Shortcuts:** desktop and/or Start menu
6. **Checking your system:** confirms the Windows version and installs the Microsoft VC++ runtime
   if it's missing
7. **Installing** → **"The studio is ready"**, with options to launch the app or open the
   Getting Started guide

The wizard uses a dark theme (Inno Setup's built-in dark style) with crimson and gold headings.

The installer writes a log you can read if something goes wrong:
`%TEMP%\Setup Log <date> #NNN.txt`.

## Upgrading
Run a newer installer over the old one. It finds the existing install folder automatically.

**Coming from HeartCast Studio (the old name)?** The KovaLive installer notices it and offers to
remove it first. Say yes: your scenes, settings, layouts and saved stream keys are kept, and
KovaLive copies them from `%APPDATA%\HeartCast` to `%APPDATA%\KovaLive` the first time it starts.
If you keep both installed, uninstalling HeartCast Studio later also unregisters the shared
virtual-camera driver; re-run the KovaLive installer afterwards to register it again.

Your settings, scenes, layouts, media library and saved stream keys live in
`%APPDATA%\KovaLive` and are never touched by upgrades. You can also update from
*Help → Check for Updates* once the build has an update feed.

Every downloaded update is checked against its SHA-256 and Authenticode signature before it runs.
The previous installer is kept in `%APPDATA%\KovaLive\updates` so you can roll back by
running it again.

## Portable mode
Unzip `KovaLive-<version>-portable.zip` and run `bin\64bit\KovaLive.exe`. To use the
virtual camera in portable mode, register it once from an admin prompt:
`regsvr32 data\obs-plugins\win-dshow\obs-virtualcam-module64.dll`

## Uninstall
*Settings → Apps → KovaLive! → Uninstall*. The uninstaller removes the program and
unregisters the virtual camera. It then asks whether to delete your personal data in
`%APPDATA%\KovaLive` (default: keep). Recordings are never deleted.

## Troubleshooting
| Problem | Fix |
|---|---|
| "The streaming engine couldn't start" | Update your GPU driver; on laptops make sure Windows uses the dedicated GPU for KovaLive |
| Black game capture | Run KovaLive and the game at the same privilege level; try Display Capture |
| Virtual camera missing in other apps | Re-run the installer and tick *Virtual Camera*; restart the other app |
| Hotkey says "in use by another app" | Pick a different combination; another program registered it first |
| VST plugin not in the list | Only 64-bit plugins load. Put VST 2 `.dll` files in `C:\Program Files\VSTPlugins` (or another folder in the User Guide); VST3 plugins in `C:\Program Files\Common Files\VST3`, then press *Rescan VST3 list* |
| Logs and crash dumps | *Help → Log Files*; crash dumps are in `%APPDATA%\KovaLive\crashes` (*Help → Crash Reports*) |
