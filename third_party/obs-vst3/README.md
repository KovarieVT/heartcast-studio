# obs-vst3 (vendored)

VST3 audio-filter host for libobs, written by **pkv** for OBS Studio
([obsproject/obs-studio#12752](https://github.com/obsproject/obs-studio/pull/12752),
commit `695a90e2e3d5b6a22e1bfbbfeb5867fc2707695f`). Licensed **GPL-3.0-or-later**
(see the header of each file). It uses the Steinberg VST3 SDK v3.8.0 (MIT).

It loads `.vst3` effect plugins as audio filters: a separate scanner process
(`obs-vst3-scanner`) finds plugins in the standard VST3 folders so a crashing plugin
can't take the app down during scanning; the filter processes audio in real time and
opens the plugin's own editor window.

## Changes made for KovaLive!

1. `plugin-main.cpp`: removed the `obs_frontend_is_safe_mode_enabled()` check. KovaLive!
   has no OBS frontend (and no OBS safe mode).
2. `VST3EditorWindow.cpp`: editor windows are parented to the application's visible
   `QMainWindow` instead of `obs_frontend_get_main_window()`.
3. Build files replaced with `CMakeLists.txt` in this folder; the OBS logo was removed
   from the Windows resource (KovaLive's icon is used instead).

No other source changes.
