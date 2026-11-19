# HeartCast Studio: Getting Started ♡

## 1. First launch
The splash screen shows real loading progress. The setup wizard then asks for:
* **Beginner** or **Advanced** mode. Beginner picks resolution, FPS and encoder from your hardware.
* Camera, microphone (with a live level meter) and speakers (with a test tone)
* Canvas size (landscape, vertical for TikTok/Shorts, or square), FPS and encoder (Advanced mode)
* Your first streaming destination and an accent colour

You can run the wizard again from *Tools → Run setup wizard*.

## 2. The workspace (dockable panels)
The **Studio** page is your control room. The preview stays in the centre. Everything else is a
**panel** you can rearrange, like in OBS or PRISM:

* **Move:** drag a panel's title bar to any edge of the window. A highlight shows where it will dock.
* **Tab together:** drop a panel onto another panel's title bar or body. They become tabs.
* **Pop out:** double-click the title bar, or use the ⧉ button. Popped-out panels are normal windows
  you can put on a second monitor, and they stay open when you switch pages.
* **Resize:** drag the gaps between panels.
* **Show/hide:** the **Panels** menu, or the **▦ Panels** button in the studio toolbar. Available panels:
  Scenes, Sources, Audio Mixer, Scene Transitions, Controls, Chat, Activity Feed, Stats.
* **Lock panels** (Panels menu) stops accidental drags while you're live.
* **Save current layout as…** stores named layouts, e.g. "Gaming" or "Just Chatting".
  **Reset panels to default** restores the original arrangement.

Your layout is saved automatically.

## 3. Scenes and sources
* **Scenes panel:** ＋ add, － remove, ⧉ duplicate, ▲▼ reorder, double-click to rename. Right-click
  for more options.
* **Sources panel:** ＋ opens every source type, grouped (Capture, Audio, Media, Text & creative,
  Widgets). 👁 hides a source, 🔒 locks it. Right-click for transform, filters, duplicate, group, order.
* **In the preview:** click to select, drag to move, drag a corner to resize (hold **Shift** to
  ignore aspect ratio), **Alt** turns off snapping, arrow keys nudge (Shift = 10 px). Double-click
  opens properties. **Ctrl+E** opens exact position, size, rotation and crop. **Ctrl+Z / Ctrl+Y**
  undo and redo.
* **Safe areas** shows title- and action-safe guides.
* **Studio mode:** edit the preview while viewers see the program, then press **Transition ➜**.
* **⛶ Fullscreen** opens a program projector on your second monitor (Esc closes it).

## 4. Going live
1. **Stream Manager → ＋ Add destination**: pick the platform, paste the Stream URL and Key. Help
   text tells you where to find them. Keys are encrypted with Windows DPAPI.
2. Twitch and YouTube can be **connected** instead. Sign in happens in your browser (we never see
   your password), and HeartCast fetches the server and key, updates your title and category, and
   brings in chat and alerts.
3. Add more destinations to **multistream**. Destinations at the default bitrate share one encode.
   Tick "different bitrate" only when a platform needs a lower one. The page shows how much upload
   bandwidth you need.
4. Press **✦ Go Live**. Each destination shows its own status, bitrate and dropped frames. You can
   stop one destination without stopping the others.
5. Use **Test connections** if something won't connect.

## 5. Recording
**● Start Recording** in Controls, or the Recording page. MKV is the default because it survives
crashes. An MP4 copy is made automatically when you stop. You get pause/resume, up to 6 audio tracks
(per-source tracks are under Mixer ⚙ → Advanced), and a **replay buffer** (*Save Replay* or a hotkey).
The Recording page lists every file with thumbnails, rename, export, remux and delete.

## 6. Audio mixer
Each source has a fader, mute, **S**olo, 🎧 monitoring and ⚙ (filters, sync offset, balance, mono,
tracks). The meter shows level and peak, and a red dot means clipping. *Controls → Mic test* helps
you set levels and add noise suppression or a compressor. Save filter chains as presets in the
filter window.

## 7. Overlays, widgets and alerts
* **Overlays:** one click creates a themed scene from a pack.
* **Widgets:** alert box, chat box, event list, stream labels, goal bars, countdown, stopwatch,
  social and sponsor panels, viewer count, text panel. Add one to the current scene, then
  double-click it to customise size, font size, pack, sound and duration.
* **Alert rules:** turn each event type on or off, set a minimum amount, and edit the message
  (`{user}`, `{amount}`, `{message}`). **Test ✦** fires a sample alert.
  Alert sounds are mixed into your stream.

## 8. Chat and activity
The Chat page (or the Chat panel) merges Twitch and YouTube. Right-click a message to delete it,
time out or ban the user, or block them. Only actions the platform actually allows are shown.
**Blocked words** and automatic flood, repeat and all-caps filtering hide messages locally.
Activity Feed lists follows, subs, gifts, raids, cheers, Super Chats and memberships.

## 9. Effects and transitions
**Effects:** colour correction, chroma key, LUTs, masks, crop, sharpen; noise suppression, gate,
compressor and EQ for the mic. AI background removal needs an NVIDIA RTX GPU (Video Effects SDK)
or the obs-backgroundremoval plugin.
**Transitions:** Fade, Cut, Swipe, Slide, Luma wipe, Stinger (your own video), plus the original
Heart Iris, Star Burst, Ribbon Sweep and Sparkle Dissolve.

## 10. Hotkeys
*Settings → Hotkeys*. On Windows, hotkeys are global, so they work while your game has focus.
Conflicts with other actions, or with keys another program already owns, are shown immediately.
Available actions: start/stop stream, recording, pause, replay save, virtual camera, mute/unmute
mic, switch to any scene, toggle the selected source, play/pause media, test alert.

## 11. Virtual camera
**Start Virtual Camera**, then choose "HeartCast Virtual Camera" in Discord, Zoom, Teams or Meet.
The camera driver must have been registered by the installer.

## 12. Performance tips
* Use a GPU encoder (NVENC, AMF or QSV) when you game and stream on the same PC.
* *Settings → Streaming → Performance profile* applies sensible defaults for your PC class.
* Decorative animations pause automatically while you're live.
* Watch the **Stats** panel. Render lag means the GPU is busy. Encoder skips mean the encoder is
  overloaded. Dropped frames mean your network is the bottleneck.
