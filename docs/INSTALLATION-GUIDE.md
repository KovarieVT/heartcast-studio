# Installation guide

## Requirements
* Windows 10 version 1809 or newer, or Windows 11, 64-bit
* A DirectX 11 GPU with an up-to-date driver
* About 400 MB of disk space, plus space for recordings
* For GPU encoding: NVIDIA GTX 10-series or newer, AMD RX 400 or newer, or Intel 6th-gen or newer

## Install
1. Run `HeartCastStudio-<version>-Setup.exe`.
   If the build isn't code-signed, SmartScreen will say "Windows protected your PC".
   Choose *More info → Run anyway* only if you built it yourself or trust where it came from.
2. **Welcome** → **License** (GPL v2+) → **Location** (default `C:\Program Files\HeartCast Studio`)
3. **Components:**
   * *Virtual Camera*: registers "HeartCast Virtual Camera" so Discord, Zoom, Teams and Meet can see it
   * *Overlay packs*, *Documentation*
4. **Shortcuts:** desktop and/or Start menu
5. **Checking your system:** confirms the Windows version and installs the Microsoft VC++ runtime
   if it's missing
6. **Installing** → **"Your streaming studio is ready!"**, with options to launch the app or open
   the Getting Started guide

The installer writes a log you can read if something goes wrong:
`%TEMP%\Setup Log <date> #NNN.txt`.

## Upgrading
Run a newer installer over the old one. It finds the existing install folder automatically.
Your settings, scenes, layouts, media library and saved stream keys live in
`%APPDATA%\HeartCast` and are never touched by upgrades. You can also update from
*Tools → Check for updates* once the build has an update feed.

Every downloaded update is checked against its SHA-256 and Authenticode signature before it runs.
The previous installer is kept in `%APPDATA%\HeartCast\updates` so you can roll back by
running it again.

## Portable mode
Unzip `HeartCastStudio-<version>-portable.zip` and run `bin\64bit\HeartCastStudio.exe`. To use the
virtual camera in portable mode, register it once from an admin prompt:
`regsvr32 data\obs-plugins\win-dshow\obs-virtualcam-module64.dll`

## Uninstall
*Settings → Apps → HeartCast Studio → Uninstall*. The uninstaller removes the program and
unregisters the virtual camera. It then asks whether to delete your personal data in
`%APPDATA%\HeartCast` (default: keep). Recordings are never deleted.

## Troubleshooting
| Problem | Fix |
|---|---|
| "The streaming engine couldn't start" | Update your GPU driver; on laptops make sure Windows uses the dedicated GPU for HeartCast |
| Black game capture | Run HeartCast and the game at the same privilege level; try Display Capture |
| Virtual camera missing in other apps | Re-run the installer and tick *Virtual Camera*; restart the other app |
| Hotkey says "in use by another app" | Pick a different combination; another program registered it first |
| Logs and crash dumps | *Tools → Open logs folder*; crash dumps are in `%APPDATA%\HeartCast\crashes` |
