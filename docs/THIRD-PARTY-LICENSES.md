# Third-party software

HeartCast Studio is free software under the **GNU General Public License v2 or later**
(see LICENSE). This is required because it links libobs. Binary distributions must include the
corresponding source code or a written offer for it.

| Component | Use | Licence |
|---|---|---|
| **libobs** and OBS Studio engine plugins (obs-ffmpeg, obs-outputs, obs-x264, win-capture, win-dshow, win-wasapi, obs-filters, obs-transitions, image-source, text, nv/qsv encoders, vst, …), © OBS Project contributors | Capture, compositing, encoding, outputs | GPL-2.0-or-later |
| **Qt 6** (Core, Gui, Widgets, Network, Multimedia, WebSockets, Svg, Concurrent), © The Qt Company | UI framework, dynamically linked | LGPL-3.0 |
| **FFmpeg** (bundled with the OBS deps) | Media decoding, muxing, AAC/Opus | LGPL-2.1+ (GPL parts as configured in obs-deps) |
| **x264** | Software H.264 encoder | GPL-2.0-or-later |
| **librist, libsrt, mbedTLS, libcurl, zlib, libdatachannel, RNNoise, Speex DSP, libvpl, AMF headers, NVENC headers** (as built into obs-deps) | Networking, TLS, noise suppression, hardware encoders | Each under its own permissive or LGPL licence; full texts ship in `data\obs-plugins` and the OBS source |
| **Microsoft Visual C++ Redistributable** | Runtime | Microsoft software licence (redistributable) |
| **Inno Setup** | Installer builder (not shipped) | Inno Setup licence |
| Fonts: the UI requests Nunito / Quicksand / Poppins (SIL OFL 1.1) if installed, and falls back to Segoe UI | Typography | SIL Open Font License 1.1 |

All artwork in this repository (logo, icons, installer images, splash, overlay packs, starter assets,
luma-wipe transitions) is original. It is generated from `resources/icons/logo.svg` and by code in
`src/ui/KawaiiPainter.cpp`, `src/engine/Engine.cpp` and `src/ui/Pages.cpp`. No third-party
characters or brand assets are used. Platform names are trademarks of their owners and are used only
to identify streaming destinations.
