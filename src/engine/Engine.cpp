#include "engine/Engine.h"
#include "auth/SecureStore.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/WidgetSource.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent>
#include <QtMath>
#include <media-io/media-remux.h>

#ifdef __linux__
#include <obs-nix-platform.h>
#include <QGuiApplication>
#endif

namespace bc {

// ===================================================================
//  helpers
// ===================================================================
static QString platformDesktopAudioId() {
#ifdef _WIN32
    return "wasapi_output_capture";
#elif defined(__APPLE__)
    return "sck_audio_capture";
#else
    return "pulse_output_capture";
#endif
}
static QString platformMicId() {
#ifdef _WIN32
    return "wasapi_input_capture";
#elif defined(__APPLE__)
    return "coreaudio_input_capture";
#else
    return "pulse_input_capture";
#endif
}

QString outputStateName(OutputState s) {
    switch (s) {
    case OutputState::Off: return QObject::tr("Offline");
    case OutputState::Starting: return QObject::tr("Connecting…");
    case OutputState::Live: return QObject::tr("LIVE");
    case OutputState::Reconnecting: return QObject::tr("Reconnecting…");
    case OutputState::Stopping: return QObject::tr("Stopping…");
    case OutputState::Error: return QObject::tr("Error");
    }
    return {};
}

static QString outputErrorText(int code, obs_output_t *out) {
    QString detail = out ? QString::fromUtf8(obs_output_get_last_error(out) ? obs_output_get_last_error(out) : "") : QString();
    QString base;
    switch (code) {
    case OBS_OUTPUT_BAD_PATH:      base = QObject::tr("Invalid server URL or file path."); break;
    case OBS_OUTPUT_CONNECT_FAILED:base = QObject::tr("Could not connect to the server. Check the URL, your firewall and network."); break;
    case OBS_OUTPUT_INVALID_STREAM:base = QObject::tr("The server rejected the stream key."); break;
    case OBS_OUTPUT_DISCONNECTED:  base = QObject::tr("Disconnected from the server."); break;
    case OBS_OUTPUT_UNSUPPORTED:   base = QObject::tr("This output/encoder combination is not supported."); break;
    case OBS_OUTPUT_NO_SPACE:      base = QObject::tr("The disk is full."); break;
    case OBS_OUTPUT_ENCODE_ERROR:  base = QObject::tr("The encoder failed. Try a different encoder or lower settings."); break;
    default:                       base = QObject::tr("Output error (code %1).").arg(code);
    }
    return detail.isEmpty() ? base : base + " " + detail;
}

QJsonObject Destination::toJson() const {
    return {{"id", id}, {"platform", platform}, {"name", name}, {"server", server}, {"keyRef", keyRef},
            {"enabled", enabled}, {"separateEncoder", separateEncoder}, {"bitrate", bitrate}};
}
Destination Destination::fromJson(const QJsonObject &o) {
    Destination d;
    d.id = o.value("id").toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    d.platform = o.value("platform").toString("custom");
    d.name = o.value("name").toString(d.platform);
    d.server = o.value("server").toString();
    d.keyRef = o.value("keyRef").toString("streamkey." + d.id);
    d.enabled = o.value("enabled").toBool(true);
    d.separateEncoder = o.value("separateEncoder").toBool(false);
    d.bitrate = o.value("bitrate").toInt(0);
    return d;
}

Engine &Engine::instance() {
    static Engine e;
    return e;
}

// ===================================================================
//  lifecycle
// ===================================================================
bool Engine::startup(const std::function<void(int, const QString &)> &progress) {
    if (m_running) return true;
    auto report = [&](int p, const QString &m) { if (progress) progress(p, m); };

#ifdef __linux__
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
        obs_set_nix_platform_display(x11->display());
    }
#endif

    report(5, tr("Waking up the engine…"));
    const QByteArray cfg = QDir::toNativeSeparators(Paths::ensure(Paths::dataRoot() + "/obs-modules")).toUtf8();
    if (!obs_startup("en-US", cfg.constData(), nullptr)) {
        Q_EMIT engineError(tr("libobs failed to start."));
        return false;
    }

    report(15, tr("Preparing your canvas…"));
    QString err;
    if (!resetVideo(&err)) {
        Q_EMIT engineError(err);
        obs_shutdown();
        return false;
    }
    report(25, tr("Tuning the audio…"));
    if (!resetAudio()) {
        Q_EMIT engineError(tr("Audio subsystem failed to initialise."));
        obs_shutdown();
        return false;
    }

    report(35, tr("Sprinkling some magic (loading plugins)…"));
    // Installed layout mirrors OBS so libobs' default search paths also work:
    //   bin/64bit/<exe>, obs-plugins/64bit/*.dll, data/obs-plugins/<module>/
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pluginBin = QDir::cleanPath(appDir + "/../../obs-plugins/64bit");
    const QString pluginData = QDir::cleanPath(appDir + "/../../data/obs-plugins/%module%");
    if (QDir(pluginBin).exists())
        obs_add_module_path(pluginBin.toUtf8().constData(), pluginData.toUtf8().constData());
    // User plugins (opt-in, see PluginManager)
    const QString userBin = Paths::plugins() + "/obs/bin";
    if (QDir(userBin).exists() && Settings::instance().b("plugins.allowUnsigned"))
        obs_add_module_path(userBin.toUtf8().constData(), (Paths::plugins() + "/obs/data/%module%").toUtf8().constData());

    // Allowlist: only engine modules load. OBS *UI* plugins (frontend-tools, obs-websocket,
    // decklink-output-ui …) expect OBS's own window and would crash here; unknown third-party
    // modules must be enabled explicitly by the user in the Plugins page.
    static const char *kEngineModules[] = {
        "image-source", "obs-ffmpeg", "obs-filters", "obs-outputs", "obs-transitions", "obs-x264",
        "rtmp-services", "text-freetype2", "obs-text", "obs-vst", "obs-webrtc", "obs-nvenc", "nv-filters",
        "obs-qsv11", "win-capture", "win-dshow", "win-wasapi", "coreaudio-encoder", "decklink", "aja",
        "linux-capture", "linux-pulseaudio", "linux-alsa", "linux-v4l2", "linux-pipewire", "linux-jack",
        "vlc-video", "mac-capture", "mac-avcapture", "mac-videotoolbox", "mac-virtualcam", "obs-libfdk"};
    for (const char *m : kEngineModules) obs_add_safe_module(m);
    for (const auto &e : Settings::instance().get("plugins.enabledObsModules").toStringList())
        obs_add_safe_module(e.toUtf8().constData());

    obs_module_failure_info mfi{};
    obs_load_all_modules2(&mfi);
    for (size_t i = 0; i < mfi.count; ++i) qWarning("Module failed to load: %s", mfi.failed_modules[i]);
    obs_module_failure_info_free(&mfi);
    obs_log_loaded_modules();
    obs_post_load_modules();

    report(60, tr("Registering cute widgets…"));
    registerWidgetSource();

    report(68, tr("Polishing transitions…"));
    generateKawaiiLumas();
    setTransition("fade_transition");

    report(75, tr("Finding your microphone…"));
    createGlobalAudio();

    m_running = true;
    report(85, tr("Opening your scenes…"));
    const QString last = Settings::instance().s("general.lastCollection");
    const QString path = last.isEmpty() ? Paths::sceneCollections() + "/default.json" : last;
    if (!QFile::exists(path) || !loadCollection(path)) {
        loadStarterCollection();
        m_collectionPath = Paths::sceneCollections() + "/default.json";
        saveCollection(m_collectionPath);
    }
    report(100, tr("Almost ready, cutie!"));
    return true;
}

void Engine::shutdown() {
    if (!m_running) return;
    stopStreaming();
    if (isRecording()) obs_output_force_stop(m_record.out);
    if (isReplayBufferActive()) obs_output_force_stop(m_replay.out);
    if (isVirtualCamActive()) obs_output_force_stop(m_vcam.out);
    if (!m_collectionPath.isEmpty()) saveCollection(m_collectionPath);

    for (auto *s : std::as_const(m_streams)) delete s;
    m_streams.clear();
    m_record.reset(); m_replay.reset(); m_vcam.reset();
    m_streamVideoEnc = nullptr; m_streamAudioEnc = nullptr;
    m_recVideoEnc = nullptr; m_recAudioEnc = nullptr;
    m_sceneSignals.clear();
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) obs_set_output_source(ch, nullptr);
    m_desktopAudio = nullptr; m_micAudio = nullptr; m_transition = nullptr;
    clearAll();
    unregisterWidgetSources();
    m_running = false;
    obs_shutdown();
    qInfo("Engine shut down. Leaked refs: %ld", bnum_allocs());
}

bool Engine::resetVideo(QString *error) {
    auto &s = Settings::instance();
    obs_video_info ovi{};
#ifdef _WIN32
    ovi.graphics_module = "libobs-d3d11";
#else
    ovi.graphics_module = "libobs-opengl";
#endif
    ovi.fps_num = uint32_t(qMax(1, s.i("video.fpsNum")));
    ovi.fps_den = uint32_t(qMax(1, s.i("video.fpsDen")));
    ovi.base_width = uint32_t(s.i("video.baseWidth"));
    ovi.base_height = uint32_t(s.i("video.baseHeight"));
    ovi.output_width = uint32_t(s.i("video.outWidth"));
    ovi.output_height = uint32_t(s.i("video.outHeight"));
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.adapter = uint32_t(s.i("advanced.gpuIndex"));
    ovi.gpu_conversion = true;
    ovi.colorspace = s.s("video.colorSpace") == "601" ? VIDEO_CS_601 : VIDEO_CS_709;
    ovi.range = s.s("video.colorRange") == "full" ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
    const QString sc = s.s("video.scaleType");
    ovi.scale_type = sc == "bilinear" ? OBS_SCALE_BILINEAR : sc == "lanczos" ? OBS_SCALE_LANCZOS
                   : sc == "area" ? OBS_SCALE_AREA : OBS_SCALE_BICUBIC;

    const int r = obs_reset_video(&ovi);
    if (r == OBS_VIDEO_SUCCESS) return true;
    if (error) {
        switch (r) {
        case OBS_VIDEO_CURRENTLY_ACTIVE: *error = tr("Stop streaming/recording before changing video settings."); break;
        case OBS_VIDEO_NOT_SUPPORTED: *error = tr("Your GPU or driver does not support the required graphics features. Update your graphics driver."); break;
        case OBS_VIDEO_INVALID_PARAM: *error = tr("Invalid video settings (resolution or FPS)."); break;
        case OBS_VIDEO_MODULE_NOT_FOUND: *error = tr("Graphics module missing — reinstall HeartCast."); break;
        default: *error = tr("Video initialisation failed (code %1).").arg(r);
        }
    }
    return false;
}

bool Engine::resetAudio() {
    auto &s = Settings::instance();
    obs_audio_info ai{};
    ai.samples_per_sec = uint32_t(s.i("audio.sampleRate"));
    ai.speakers = s.s("audio.channels") == "mono" ? SPEAKERS_MONO : SPEAKERS_STEREO;
    return obs_reset_audio(&ai);
}

void Engine::createGlobalAudio() {
    auto &s = Settings::instance();
    auto make = [](const QString &id, const QString &name, const QString &dev) {
        OBSDataAutoRelease st = obs_data_create();
        obs_data_set_string(st, "device_id", dev.isEmpty() ? "default" : dev.toUtf8().constData());
        return obs_source_create(id.toUtf8().constData(), name.toUtf8().constData(), st, nullptr);
    };
    m_desktopAudio = make(platformDesktopAudioId(), tr("Desktop Audio"), s.s("audio.desktopDevice"));
    m_micAudio = make(platformMicId(), tr("Microphone"), s.s("audio.micDevice"));
    if (m_desktopAudio) obs_set_output_source(1, m_desktopAudio);
    if (m_micAudio) obs_set_output_source(3, m_micAudio);
    if (!m_desktopAudio || !m_micAudio)
        qWarning("Global audio capture plugin unavailable (desktop=%p mic=%p)",
                 (void *)m_desktopAudio.Get(), (void *)m_micAudio.Get());
}

// ===================================================================
//  discovery
// ===================================================================
static QString categorize(const QString &id) {
    if (id.contains("capture") && (id.contains("audio") || id.contains("wasapi") || id.contains("pulse")
        || id.contains("alsa") || id.contains("jack") || id.contains("coreaudio")))
        return "audio";
    if (id.contains("capture") || id.contains("dshow") || id.contains("v4l2") || id.contains("decklink")
        || id.contains("ndi") || id.contains("pipewire") || id.contains("xshm") || id.contains("xcomposite"))
        return "capture";
    if (id.contains("image") || id.contains("ffmpeg") || id.contains("vlc") || id.contains("slideshow"))
        return "media";
    if (id.startsWith("bc_")) return "widget";
    return "creative";
}

QVector<SourceTypeInfo> Engine::sourceTypes() const {
    QVector<SourceTypeInfo> out;
    const char *id = nullptr, *unv = nullptr;
    for (size_t i = 0; obs_enum_input_types2(i, &id, &unv); ++i) {
        const uint32_t caps = obs_get_source_output_flags(id);
        if (caps & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_CAP_OBSOLETE)) continue;
        const QString sid = QString::fromUtf8(id);
        if (sid == "bc_widget" || sid == "scene" || sid == "group") continue;
        SourceTypeInfo t;
        t.id = sid;
        t.name = QString::fromUtf8(obs_source_get_display_name(id));
        t.video = caps & OBS_SOURCE_VIDEO;
        t.audio = caps & OBS_SOURCE_AUDIO;
        t.category = categorize(sid);
        out << t;
    }
    // App-provided native types
    out << SourceTypeInfo{"bc_gradient", tr("Gradient ♡"), "creative", true, false};
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return out;
}

QVector<SourceTypeInfo> Engine::filterTypes(bool audio) const {
    QVector<SourceTypeInfo> out;
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
        const uint32_t caps = obs_get_source_output_flags(id);
        if (caps & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_CAP_OBSOLETE)) continue;
        const bool isAudio = caps & OBS_SOURCE_AUDIO, isVideo = caps & OBS_SOURCE_VIDEO;
        if (audio ? !isAudio : !isVideo) continue;
        out << SourceTypeInfo{QString::fromUtf8(id), QString::fromUtf8(obs_source_get_display_name(id)),
                              "filter", isVideo, isAudio};
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return out;
}

QStringList Engine::transitionTypes() const {
    QStringList out;
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_transition_types(i, &id); ++i) out << QString::fromUtf8(id);
    return out;
}

QVector<EncoderInfo> Engine::videoEncoders() const {
    QVector<EncoderInfo> out;
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
        if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) continue;
        const uint32_t caps = obs_get_encoder_caps(id);
        if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) continue;
        EncoderInfo e;
        e.id = QString::fromUtf8(id);
        e.name = QString::fromUtf8(obs_encoder_get_display_name(id));
        e.codec = QString::fromUtf8(obs_get_encoder_codec(id));
        const QString l = e.id.toLower();
        e.family = l.contains("x264") ? "x264" : l.contains("nvenc") ? "nvenc"
                 : l.contains("amf") ? "amf" : l.contains("qsv") ? "qsv" : "other";
        e.hardware = e.family != "x264" && e.family != "other";
        if (l.contains("vaapi")) { e.family = "vaapi"; e.hardware = true; }
        out << e;
    }
    return out;
}

QString Engine::bestEncoder(const QString &codec) const {
    const auto encs = videoEncoders();
    // Prefer GPU encoders so gaming + streaming on one PC stays smooth.
    for (const char *fam : {"nvenc", "amf", "qsv", "vaapi", "x264", "other"})
        for (const auto &e : encs)
            if (e.family == fam && e.codec == codec) return e.id;
    return encs.isEmpty() ? QString() : encs.first().id;
}

// ===================================================================
//  scenes
// ===================================================================
obs_source_t *Engine::sceneSource(const QString &name) const {
    OBSSourceAutoRelease s = obs_get_source_by_name(name.toUtf8().constData());
    if (!s || !obs_source_is_scene(s)) return nullptr;
    return s.Get();   // scenes are kept alive by m_sceneOrder ownership below (obs keeps a ref)
}

obs_scene_t *Engine::scene(const QString &name) const {
    obs_source_t *s = sceneSource(name);
    return s ? obs_scene_from_source(s) : nullptr;
}

QString Engine::uniqueSceneName(const QString &base) const {
    QString n = base.trimmed().isEmpty() ? tr("Scene") : base.trimmed();
    QString cand = n;
    for (int i = 2;; ++i) {
        OBSSourceAutoRelease s = obs_get_source_by_name(cand.toUtf8().constData());
        if (!s) return cand;
        cand = QStringLiteral("%1 %2").arg(n).arg(i);
    }
}

QString Engine::uniqueSourceName(const QString &base) const { return uniqueSceneName(base); }

QString Engine::createScene(const QString &name) {
    const QString n = uniqueSceneName(name);
    obs_scene_t *sc = obs_scene_create(n.toUtf8().constData());
    if (!sc) return {};
    // We intentionally keep the creation reference; it is dropped in removeScene()/clearAll().
    m_sceneOrder << n;
    if (m_programScene.isEmpty()) transitionTo(n);
    if (m_previewScene.isEmpty()) setPreviewScene(n);
    Q_EMIT scenesChanged();
    return n;
}

bool Engine::removeScene(const QString &name) {
    if (m_sceneOrder.size() <= 1) return false;   // always keep one scene
    obs_source_t *src = sceneSource(name);
    if (!src) return false;
    const int idx = m_sceneOrder.indexOf(name);
    m_sceneOrder.removeAll(name);
    const QString fallback = m_sceneOrder.value(qMax(0, idx - 1));
    if (m_programScene == name) transitionTo(fallback);
    if (m_previewScene == name) setPreviewScene(fallback);
    obs_source_remove(src);
    obs_source_release(src);   // creation ref
    Q_EMIT scenesChanged();
    return true;
}

bool Engine::renameScene(const QString &from, const QString &to) {
    obs_source_t *src = sceneSource(from);
    const QString t = to.trimmed();
    if (!src || t.isEmpty()) return false;
    OBSSourceAutoRelease existing = obs_get_source_by_name(t.toUtf8().constData());
    if (existing) return false;
    obs_source_set_name(src, t.toUtf8().constData());
    m_sceneOrder.replace(m_sceneOrder.indexOf(from), t);
    if (m_programScene == from) m_programScene = t;
    if (m_previewScene == from) m_previewScene = t;
    Q_EMIT scenesChanged();
    return true;
}

QString Engine::duplicateScene(const QString &name) {
    obs_scene_t *sc = scene(name);
    if (!sc) return {};
    const QString n = uniqueSceneName(name + tr(" (copy)"));
    obs_scene_t *dup = obs_scene_duplicate(sc, n.toUtf8().constData(), OBS_SCENE_DUP_REFS);
    if (!dup) return {};
    m_sceneOrder.insert(m_sceneOrder.indexOf(name) + 1, n);
    Q_EMIT scenesChanged();
    return n;
}

void Engine::moveScene(const QString &name, int newIndex) {
    const int from = m_sceneOrder.indexOf(name);
    if (from < 0) return;
    newIndex = qBound(0, newIndex, int(m_sceneOrder.size()) - 1);
    m_sceneOrder.move(from, newIndex);
    Q_EMIT scenesChanged();
}

void Engine::setPreviewScene(const QString &name) {
    if (!sceneSource(name)) return;
    m_previewScene = name;
    if (!m_studioMode && name != m_programScene) transitionTo(name);
    Q_EMIT previewSceneChanged(name);
}

void Engine::transitionTo(const QString &name) {
    obs_source_t *src = sceneSource(name);
    if (!src || !m_transition) return;
    if (m_programScene.isEmpty())
        obs_transition_set(m_transition, src);
    else
        obs_transition_start(m_transition, OBS_TRANSITION_MODE_AUTO, uint32_t(m_transitionMs), src);
    m_programScene = name;
    if (!m_studioMode) m_previewScene = name;
    Q_EMIT programSceneChanged(name);
}

void Engine::setStudioMode(bool on) {
    m_studioMode = on;
    if (!on) setPreviewScene(m_programScene);
}

// ===================================================================
//  sources
// ===================================================================
QString Engine::makeGradientImage(const QString &c1, const QString &c2, int w, int h, int angleDeg) {
    QImage img(w, h, QImage::Format_ARGB32);
    QPainter p(&img);
    const double a = qDegreesToRadians(double(angleDeg));
    const QPointF c(w / 2.0, h / 2.0), d(std::cos(a) * w / 2.0, std::sin(a) * h / 2.0);
    QLinearGradient g(c - d, c + d);
    g.setColorAt(0, QColor(c1));
    g.setColorAt(1, QColor(c2));
    p.fillRect(img.rect(), g);
    p.end();
    const QString path = Paths::cache() + QStringLiteral("/gradient_%1_%2_%3.png")
        .arg(QString(c1).remove('#'), QString(c2).remove('#')).arg(angleDeg);
    if (!QFile::exists(path)) img.save(path);
    return path;
}

obs_sceneitem_t *Engine::addSource(const QString &sceneName, const QString &typeId,
                                   const QString &name, obs_data_t *settings) {
    obs_scene_t *sc = scene(sceneName);
    if (!sc) return nullptr;
    QString realType = typeId;
    OBSDataAutoRelease st = settings ? obs_data_create_from_json(obs_data_get_json(settings)) : obs_data_create();

    if (typeId == "bc_gradient") {
        const QString c1 = obs_data_has_user_value(st, "bc_c1") ? obs_data_get_string(st, "bc_c1") : "#FFDCEB";
        const QString c2 = obs_data_has_user_value(st, "bc_c2") ? obs_data_get_string(st, "bc_c2") : "#D2F3E7";
        obs_data_set_string(st, "bc_c1", c1.toUtf8().constData());
        obs_data_set_string(st, "bc_c2", c2.toUtf8().constData());
        const auto &s = Settings::instance();
        obs_data_set_string(st, "file", makeGradientImage(c1, c2, s.i("video.baseWidth"), s.i("video.baseHeight")).toUtf8().constData());
        realType = "image_source";
    }

    const QString n = uniqueSourceName(name);
    OBSSourceAutoRelease src = obs_source_create(realType.toUtf8().constData(), n.toUtf8().constData(), st, nullptr);
    if (!src) {
        Q_EMIT engineError(tr("Source type \"%1\" is not available on this system.").arg(typeId));
        return nullptr;
    }
    obs_sceneitem_t *item = obs_scene_add(sc, src);
    Q_EMIT sceneItemsChanged(sceneName);
    return item;
}

obs_sceneitem_t *Engine::addExistingSource(const QString &sceneName, obs_source_t *src) {
    obs_scene_t *sc = scene(sceneName);
    if (!sc || !src) return nullptr;
    obs_sceneitem_t *item = obs_scene_add(sc, src);
    Q_EMIT sceneItemsChanged(sceneName);
    return item;
}

void Engine::fitItemToCanvas(obs_sceneitem_t *item, double scale, const QString &align) {
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    const float cw = float(ovi.base_width), ch = float(ovi.base_height);
    vec2 bounds{cw * float(scale), ch * float(scale)};
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds(item, &bounds);
    obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
    obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
    vec2 pos{0, 0};
    const float margin = 24.f;
    if (align == "center") { pos.x = (cw - bounds.x) / 2; pos.y = (ch - bounds.y) / 2; }
    else if (align == "bottom_right") { pos.x = cw - bounds.x - margin; pos.y = ch - bounds.y - margin; }
    else if (align == "bottom_left") { pos.x = margin; pos.y = ch - bounds.y - margin; }
    else if (align == "top_right") { pos.x = cw - bounds.x - margin; pos.y = margin; }
    else if (align == "top_left") { pos.x = margin; pos.y = margin; }
    obs_sceneitem_set_pos(item, &pos);
}

// ===================================================================
//  transitions (+ procedurally generated kawaii luma wipes)
// ===================================================================
QString Engine::kawaiiLumaDir() {
    obs_module_t *mod = obs_get_module("obs-transitions");
    if (mod) {
        char *p = obs_find_module_file(mod, "luma_wipes");
        if (p) { QString d = QString::fromUtf8(p); bfree(p); return d; }
    }
    return {};
}

static QImage makeLuma(const QString &kind, int w, int h) {
    QImage img(w, h, QImage::Format_Grayscale8);
    const double aspect = double(w) / h;
    auto heartInside = [](double x, double y) {   // classic implicit heart, y up
        const double a = x * x + y * y - 1.0;
        return a * a * a - x * x * y * y * y <= 0.0;
    };
    for (int py = 0; py < h; ++py) {
        uchar *row = img.scanLine(py);
        for (int px = 0; px < w; ++px) {
            const double x = (double(px) / w - 0.5) * 2.0 * aspect;
            const double y = -((double(py) / h - 0.5) * 2.0) + 0.15;
            double v = 0;
            if (kind == "heart") {
                double lo = 0.0, hi = 3.0;                 // smallest heart scale containing point
                for (int k = 0; k < 18; ++k) {
                    const double m = (lo + hi) / 2;
                    (heartInside(x / m, y / m) ? hi : lo) = m;
                }
                v = hi / 3.0;
            } else if (kind == "star") {
                const double r = std::hypot(x, y), th = std::atan2(y, x);
                v = r / (1.0 + 0.45 * std::cos(5.0 * th + M_PI / 2)) / 2.4;
            } else if (kind == "ribbon") {               // wavy diagonal ribbons sweeping left→right
                const double t = (x + aspect) / (2 * aspect);
                v = 0.85 * t + 0.15 * (0.5 + 0.5 * std::sin(y * 9.0 + x * 2.0));
            } else if (kind == "sparkle") {              // soft dither of sparkles
                const quint32 hsh = quint32(px * 73856093u) ^ quint32(py * 19349663u);
                v = 0.6 * ((hsh % 1000) / 1000.0) + 0.4 * (std::hypot(x, y) / 2.2);
            }
            row[px] = uchar(qBound(0.0, v, 1.0) * 255);
        }
    }
    return img;
}

void Engine::generateKawaiiLumas() {
    const QString sysDir = kawaiiLumaDir();
    const QString dir = (!sysDir.isEmpty() && QFileInfo(sysDir).isWritable()) ? sysDir
                                                                              : Paths::ensure(Paths::cache() + "/luma");
    for (const QString kind : {"heart", "star", "ribbon", "sparkle"}) {
        const QString path = dir + "/heartcast-" + kind + ".png";
        if (!QFile::exists(path)) makeLuma(kind, 480, 270).scaled(1920, 1080, Qt::IgnoreAspectRatio,
                                                                   Qt::SmoothTransformation).save(path);
    }
}

QString Engine::transitionId() const {
    return m_transition ? QString::fromUtf8(obs_source_get_id(m_transition)) : QString();
}

void Engine::setTransition(const QString &typeId, obs_data_t *settings) {
    QString id = typeId;
    OBSDataAutoRelease st = settings ? obs_data_create_from_json(obs_data_get_json(settings)) : obs_data_create();
    // Kawaii luma wipes are regular luma-wipe transitions pointed at our generated images.
    if (typeId.startsWith("bc_luma_")) {
        const QString kind = typeId.mid(8);
        QString file = "heartcast-" + kind + ".png";
        const QString sys = kawaiiLumaDir();
        if (sys.isEmpty() || !QFile::exists(sys + "/" + file))   // stored in cache → relative from luma dir
            file = QDir(sys.isEmpty() ? Paths::cache() : sys).relativeFilePath(Paths::cache() + "/luma/" + file);
        obs_data_set_string(st, "luma_image", file.toUtf8().constData());
        obs_data_set_double(st, "luma_softness", 0.12);
        id = "wipe_transition";
    }
    obs_source_t *trs = obs_source_create_private(id.toUtf8().constData(), "HeartCast Transition", st);
    if (!trs) { Q_EMIT engineError(tr("Transition type unavailable: %1").arg(typeId)); return; }
    obs_source_t *current = nullptr;
    if (m_transition) current = obs_transition_get_active_source(m_transition);
    if (current) { obs_transition_set(trs, current); obs_source_release(current); }
    m_transition = trs;   // takes ownership of the creation reference
    obs_set_output_source(0, m_transition);
}

// ===================================================================
//  audio devices
// ===================================================================
QVector<QPair<QString, QString>> Engine::audioDevices(bool input) const {
    QVector<QPair<QString, QString>> out;
    const QByteArray id = (input ? platformMicId() : platformDesktopAudioId()).toUtf8();
    obs_properties_t *props = obs_get_source_properties(id.constData());
    if (!props) return out;
    if (obs_property_t *p = obs_properties_get(props, "device_id")) {
        for (size_t i = 0; i < obs_property_list_item_count(p); ++i)
            out << qMakePair(QString::fromUtf8(obs_property_list_item_string(p, i)),
                             QString::fromUtf8(obs_property_list_item_name(p, i)));
    }
    obs_properties_destroy(props);
    return out;
}

void Engine::setAudioDevice(bool input, const QString &deviceId) {
    obs_source_t *src = input ? m_micAudio.Get() : m_desktopAudio.Get();
    if (!src) return;
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "device_id", deviceId.toUtf8().constData());
    obs_source_update(src, st);
    Settings::instance().set(input ? "audio.micDevice" : "audio.desktopDevice", deviceId);
}

void Engine::setMonitoringDevice(const QString &name, const QString &id) {
    obs_set_audio_monitoring_device(name.toUtf8().constData(), id.toUtf8().constData());
    Settings::instance().set("audio.monitorDevice", id);
}

// ===================================================================
//  encoders
// ===================================================================
static void applyEncoderSettings(obs_data_t *st, const QString &id, const QString &rc, int kbps,
                                 const QString &presetLvl, int cqp) {
    auto &s = Settings::instance();
    const QString l = id.toLower();
    obs_data_set_string(st, "rate_control", rc.toUtf8().constData());
    obs_data_set_int(st, "bitrate", kbps);
    obs_data_set_int(st, "max_bitrate", int(kbps * 1.5));
    obs_data_set_int(st, "keyint_sec", qMax(1, s.i("output.keyintSec")));
    obs_data_set_string(st, "profile", s.s("output.profile").toUtf8().constData());
    const int bf = s.i("output.bframes");
    obs_data_set_int(st, "bf", bf);
    obs_data_set_int(st, "bframes", bf);
    if (cqp > 0) { obs_data_set_int(st, "cqp", cqp); obs_data_set_int(st, "crf", cqp); obs_data_set_int(st, "qpi", cqp); obs_data_set_int(st, "qpp", cqp); }

    const int lvl = presetLvl == "performance" ? 0 : presetLvl == "quality" ? 2 : 1;
    if (l.contains("x264")) {
        obs_data_set_string(st, "preset", lvl == 0 ? "superfast" : lvl == 1 ? "veryfast" : "faster");
        if (cqp > 0) obs_data_set_string(st, "rate_control", "CRF");
    } else if (l.contains("nvenc")) {
        obs_data_set_string(st, "preset2", lvl == 0 ? "p2" : lvl == 1 ? "p5" : "p6");
        obs_data_set_string(st, "preset", lvl == 0 ? "p2" : lvl == 1 ? "p5" : "p6");
        obs_data_set_string(st, "tune", "hq");
        obs_data_set_string(st, "multipass", "qres");
    } else if (l.contains("amf")) {
        obs_data_set_string(st, "preset", lvl == 0 ? "speed" : lvl == 1 ? "balanced" : "quality");
    } else if (l.contains("qsv")) {
        obs_data_set_string(st, "target_usage", lvl == 0 ? "TU7" : lvl == 1 ? "TU4" : "TU2");
    }
}

obs_encoder_t *Engine::makeVideoEncoder(const QString &name, int bitrateOverride) {
    auto &s = Settings::instance();
    QString id = s.s("output.encoder");
    if (id.isEmpty() || id == "auto" || !obs_get_encoder_codec(id.toUtf8().constData())) id = bestEncoder("h264");
    OBSDataAutoRelease st = obs_data_create();
    applyEncoderSettings(st, id, s.s("output.rateControl"),
                         bitrateOverride > 0 ? bitrateOverride : s.i("output.bitrate"), s.s("output.preset"), 0);
    obs_encoder_t *enc = obs_video_encoder_create(id.toUtf8().constData(), name.toUtf8().constData(), st, nullptr);
    if (enc) obs_encoder_set_video(enc, obs_get_video());
    else qWarning("Failed to create video encoder %s", qPrintable(id));
    return enc;
}

obs_encoder_t *Engine::makeAudioEncoder(const QString &name, size_t mixIdx) {
    auto &s = Settings::instance();
    const bool opus = s.s("output.audioCodec") == "opus";
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_int(st, "bitrate", s.i("output.audioBitrate"));
    obs_encoder_t *enc = obs_audio_encoder_create(opus ? "ffmpeg_opus" : "ffmpeg_aac", name.toUtf8().constData(),
                                                  st, mixIdx, nullptr);
    if (enc) obs_encoder_set_audio(enc, obs_get_audio());
    return enc;
}

// ===================================================================
//  output signal plumbing (obs threads → Qt main thread)
// ===================================================================
namespace {
struct SigCtx { Engine *e; QString tag; QString what; };
} // namespace

void Engine::connectOutputSignals(OutHolder &h, const QString &tag) {
    // SigCtx objects live for the process lifetime (tiny) so late obs callbacks are always safe.
    for (const char *what : {"start", "stop", "reconnect", "reconnect_success", "saved"}) {
        auto *ctx = new SigCtx{this, tag, QString::fromLatin1(what)};
        h.sigs.emplace_back(obs_output_get_signal_handler(h.out), what,
            [](void *data, calldata_t *cd) {
                auto *c = static_cast<SigCtx *>(data);
                const int code = int(calldata_int(cd, "code"));
                Engine *e = c->e; const QString tag = c->tag, what = c->what;
                QMetaObject::invokeMethod(e, [e, tag, what, code] { e->onOutputSignal(tag, what, code); },
                                          Qt::QueuedConnection);
            }, ctx);
    }
}

void Engine::onOutputSignal(const QString &tag, const QString &what, int code) {
    if (tag.startsWith("stream:")) {
        const QString id = tag.mid(7);
        StreamSlot *slot = m_streams.value(id);
        if (!slot) return;
        auto &st = slot->status;
        if (what == "start") { st.state = OutputState::Live; st.startedMs = QDateTime::currentMSecsSinceEpoch(); st.error.clear(); }
        else if (what == "reconnect") st.state = OutputState::Reconnecting;
        else if (what == "reconnect_success") st.state = OutputState::Live;
        else if (what == "stop") {
            if (code != OBS_OUTPUT_SUCCESS) {
                st.state = OutputState::Error;
                st.error = outputErrorText(code, slot->o.out);
                Q_EMIT engineError(QStringLiteral("%1: %2").arg(slot->name, st.error));
            } else st.state = OutputState::Off;
            // Keep the errored slot around briefly so the UI can show why; drop on next start.
            if (code == OBS_OUTPUT_SUCCESS) { delete m_streams.take(id); }
            else { slot->o.reset(); slot->ownVideoEnc = nullptr; }
            releaseStreamEncodersIfIdle();
            if (!isStreaming() && Settings::instance().b("recording.autoRecordWhenLive") && isRecording())
                stopRecording();
        }
        Q_EMIT destinationStateChanged(id);
        Q_EMIT streamingStateChanged();
    } else if (tag == "record") {
        if (what == "stop") {
            if (code != OBS_OUTPUT_SUCCESS) Q_EMIT engineError(tr("Recording stopped: %1").arg(outputErrorText(code, m_record.out)));
            m_record.reset();
            m_recVideoEnc = nullptr; m_recAudioEnc = nullptr;
            releaseStreamEncodersIfIdle();
            if (Settings::instance().b("recording.remuxToMp4") && m_lastRecording.endsWith(".mkv"))
                remuxToMp4(m_lastRecording);
        }
        Q_EMIT recordingStateChanged();
    } else if (tag == "replay") {
        if (what == "saved") {
            proc_handler_t *ph = obs_output_get_proc_handler(m_replay.out);
            calldata_t cd{};
            proc_handler_call(ph, "get_last_replay", &cd);
            const QString path = QString::fromUtf8(calldata_string(&cd, "path"));
            calldata_free(&cd);
            Q_EMIT replaySaved(path);
        } else if (what == "stop") {
            if (code != OBS_OUTPUT_SUCCESS) Q_EMIT engineError(tr("Replay buffer stopped: %1").arg(outputErrorText(code, m_replay.out)));
            m_replay.reset();
            releaseStreamEncodersIfIdle();
        }
        Q_EMIT replayBufferStateChanged();
    } else if (tag == "vcam") {
        if (what == "stop") m_vcam.reset();
        Q_EMIT virtualCamStateChanged();
    }
}

void Engine::releaseStreamEncodersIfIdle() {
    for (auto *s : std::as_const(m_streams))
        if (s->o.out && obs_output_active(s->o.out)) return;
    if (m_record.out && obs_output_active(m_record.out)) return;
    if (m_replay.out && obs_output_active(m_replay.out)) return;
    m_streamVideoEnc = nullptr;
    m_streamAudioEnc = nullptr;
}

// ===================================================================
//  streaming (multistream with shared encoders)
// ===================================================================
bool Engine::startStreaming(const QVector<Destination> &dests, QString *error) {
    QStringList failures;
    auto &s = Settings::instance();
    int started = 0;
    for (const auto &d : dests) {
        if (!d.enabled) continue;
        if (auto *old = m_streams.value(d.id)) {
            if (old->o.out && obs_output_active(old->o.out)) continue;   // already live
            delete m_streams.take(d.id);
        }
        const QByteArray key = SecureStore::get(d.keyRef);
        if (d.server.trimmed().isEmpty() || key.isEmpty()) {
            failures << tr("%1: server URL or stream key is missing.").arg(d.name);
            continue;
        }
        auto *slot = new StreamSlot;
        slot->name = d.name;
        OBSDataAutoRelease ss = obs_data_create();
        obs_data_set_string(ss, "server", d.server.trimmed().toUtf8().constData());
        obs_data_set_string(ss, "key", key.constData());
        slot->service = obs_service_create("rtmp_custom", ("svc-" + d.id).toUtf8().constData(), ss, nullptr);
        slot->o.out = obs_output_create("rtmp_output", ("out-" + d.id).toUtf8().constData(), nullptr, nullptr);
        if (!slot->service || !slot->o.out) {
            failures << tr("%1: RTMP output plugin unavailable.").arg(d.name);
            delete slot; continue;
        }
        obs_encoder_t *venc = nullptr;
        if (d.separateEncoder && d.bitrate > 0) {
            slot->ownVideoEnc = makeVideoEncoder("venc-" + d.id, d.bitrate);
            venc = slot->ownVideoEnc;
        } else {
            if (!m_streamVideoEnc) m_streamVideoEnc = makeVideoEncoder("Stream Video", 0);
            venc = m_streamVideoEnc;
        }
        if (!m_streamAudioEnc) m_streamAudioEnc = makeAudioEncoder("Stream Audio", 0);
        if (!venc || !m_streamAudioEnc) {
            failures << tr("%1: could not create encoders.").arg(d.name);
            delete slot; continue;
        }
        obs_output_set_video_encoder(slot->o.out, venc);
        obs_output_set_audio_encoder(slot->o.out, m_streamAudioEnc, 0);
        obs_output_set_service(slot->o.out, slot->service);
        obs_output_set_reconnect_settings(slot->o.out, s.i("output.reconnectRetries"), s.i("output.reconnectDelaySec"));
        connectOutputSignals(slot->o, "stream:" + d.id);
        slot->status.state = OutputState::Starting;
        m_streams.insert(d.id, slot);
        if (!obs_output_start(slot->o.out)) {
            const char *le = obs_output_get_last_error(slot->o.out);
            slot->status.state = OutputState::Error;
            slot->status.error = le ? QString::fromUtf8(le) : tr("Failed to start output.");
            failures << QStringLiteral("%1: %2").arg(d.name, slot->status.error);
            slot->o.reset();
        } else ++started;
        Q_EMIT destinationStateChanged(d.id);
    }
    if (started > 0 && s.b("recording.autoRecordWhenLive") && !isRecording()) startRecording();
    Q_EMIT streamingStateChanged();
    if (!failures.isEmpty() && error) *error = failures.join('\n');
    return started > 0;
}

void Engine::stopDestination(const QString &destId) {
    StreamSlot *slot = m_streams.value(destId);
    if (!slot) return;
    if (slot->o.out && obs_output_active(slot->o.out)) {
        slot->status.state = OutputState::Stopping;
        obs_output_stop(slot->o.out);
    } else {
        delete m_streams.take(destId);
        releaseStreamEncodersIfIdle();
    }
    Q_EMIT destinationStateChanged(destId);
    Q_EMIT streamingStateChanged();
}

void Engine::stopStreaming() {
    for (const QString &id : m_streams.keys()) stopDestination(id);
}

bool Engine::isStreaming() const {
    for (auto *s : m_streams) if (s->o.out && obs_output_active(s->o.out)) return true;
    return false;
}

DestinationStatus Engine::destinationStatus(const QString &destId) const {
    auto *s = m_streams.value(destId);
    return s ? s->status : DestinationStatus{};
}

void Engine::pollOutputStats() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto *s : std::as_const(m_streams)) {
        if (!s->o.out || !obs_output_active(s->o.out)) continue;
        const quint64 bytes = obs_output_get_total_bytes(s->o.out);
        if (s->lastBytesMs > 0 && now > s->lastBytesMs)
            s->status.kbps = double(bytes - s->lastBytes) * 8.0 / double(now - s->lastBytesMs);
        s->lastBytes = bytes; s->lastBytesMs = now;
        s->status.droppedFrames = obs_output_get_frames_dropped(s->o.out);
        s->status.totalFrames = obs_output_get_total_frames(s->o.out);
    }
}

EngineStats Engine::stats() const {
    EngineStats st;
    if (!m_running) return st;
    st.activeFps = obs_get_active_fps();
    st.renderLagged = int(obs_get_lagged_frames());
    st.renderTotal = int(obs_get_total_frames());
    video_t *v = obs_get_video();
    st.encoderSkipped = int(video_output_get_skipped_frames(v));
    st.encoderTotal = int(video_output_get_total_frames(v));
    st.avgFrameMs = double(obs_get_average_frame_time_ns()) / 1e6;
    return st;
}

// ===================================================================
//  recording / replay buffer / virtual camera
// ===================================================================
static QString recordingQualityCqp(const QString &q) {
    return q == "high" ? "20" : q == "indistinguishable" ? "16" : q == "lossless" ? "0" : "";
}

bool Engine::startRecording(QString *error) {
    if (isRecording()) return true;
    auto &s = Settings::instance();
    const QString dir = Paths::ensure(s.s("recording.path").isEmpty() ? Paths::defaultRecordings() : s.s("recording.path"));
    if (!QFileInfo(dir).isWritable()) { if (error) *error = tr("Recording folder is not writable: %1").arg(dir); return false; }
    QString fmt = s.s("recording.format").toLower();
    if (!QStringList{"mkv", "mp4", "mov", "flv", "ts"}.contains(fmt)) fmt = "mkv";
    m_lastRecording = dir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + "." + fmt;

    m_record.out = obs_output_create("ffmpeg_muxer", "HeartCast Recording", nullptr, nullptr);
    if (!m_record.out) { if (error) *error = tr("Recording plugin (obs-ffmpeg) missing."); return false; }
    OBSDataAutoRelease os = obs_data_create();
    obs_data_set_string(os, "path", m_lastRecording.toUtf8().constData());
    obs_output_update(m_record.out, os);

    const QString q = s.s("recording.quality");
    obs_encoder_t *venc = nullptr;
    if (q == "stream" || q.isEmpty()) {
        if (!m_streamVideoEnc) m_streamVideoEnc = makeVideoEncoder("Stream Video", 0);
        venc = m_streamVideoEnc;
    } else {
        QString id = s.s("output.encoder");
        if (id.isEmpty() || id == "auto") id = bestEncoder("h264");
        OBSDataAutoRelease st = obs_data_create();
        applyEncoderSettings(st, id, "CQP", 40000, "quality", recordingQualityCqp(q).toInt() == 0 && q == "lossless" ? 1 : recordingQualityCqp(q).toInt());
        m_recVideoEnc = obs_video_encoder_create(id.toUtf8().constData(), "Recording Video", st, nullptr);
        if (m_recVideoEnc) obs_encoder_set_video(m_recVideoEnc, obs_get_video());
        venc = m_recVideoEnc;
    }
    if (!venc) { m_record.reset(); if (error) *error = tr("Could not create the recording encoder."); return false; }
    obs_output_set_video_encoder(m_record.out, venc);

    const int tracks = qBound(1, s.i("recording.tracks"), 6);
    if (tracks == 1) {
        if (!m_streamAudioEnc) m_streamAudioEnc = makeAudioEncoder("Stream Audio", 0);
        obs_output_set_audio_encoder(m_record.out, m_streamAudioEnc, 0);
    } else {
        for (int t = 0; t < tracks; ++t) {
            obs_encoder_t *ae = makeAudioEncoder(QStringLiteral("Rec Track %1").arg(t + 1), size_t(t));
            obs_output_set_audio_encoder(m_record.out, ae, size_t(t));
            obs_encoder_release(ae);   // output holds its own reference
        }
    }
    connectOutputSignals(m_record, "record");
    if (!obs_output_start(m_record.out)) {
        const char *le = obs_output_get_last_error(m_record.out);
        if (error) *error = le ? QString::fromUtf8(le) : tr("Recording failed to start.");
        m_record.reset();
        return false;
    }
    Q_EMIT recordingStateChanged();
    return true;
}

void Engine::stopRecording() { if (isRecording()) obs_output_stop(m_record.out); }
bool Engine::pauseRecording(bool pause) {
    if (!isRecording()) return false;
    const bool ok = obs_output_pause(m_record.out, pause);
    Q_EMIT recordingStateChanged();
    return ok;
}
bool Engine::isRecording() const { return m_record.out && obs_output_active(m_record.out); }
bool Engine::isRecordingPaused() const { return isRecording() && obs_output_paused(m_record.out); }

void Engine::remuxToMp4(const QString &mkvPath) {
    QString mp4 = mkvPath; mp4.chop(4); mp4 += ".mp4";
    (void)QtConcurrent::run([mkvPath, mp4] {
        media_remux_job_t job = nullptr;
        if (!media_remux_job_create(&job, mkvPath.toUtf8().constData(), mp4.toUtf8().constData())) {
            qWarning("Remux could not open %s", qPrintable(mkvPath));
            return;
        }
        const bool ok = media_remux_job_process(job, nullptr, nullptr);
        media_remux_job_destroy(job);
        qInfo("Remux %s → %s: %s", qPrintable(mkvPath), qPrintable(mp4), ok ? "ok" : "failed");
        // The MKV is kept as the safe original; the user can delete it from the Recording library.
    });
}

bool Engine::startReplayBuffer(QString *error) {
    if (isReplayBufferActive()) return true;
    auto &s = Settings::instance();
    m_replay.out = obs_output_create("replay_buffer", "HeartCast Replay", nullptr, nullptr);
    if (!m_replay.out) { if (error) *error = tr("Replay buffer plugin missing."); return false; }
    OBSDataAutoRelease os = obs_data_create();
    obs_data_set_string(os, "directory", Paths::ensure(s.s("recording.path")).toUtf8().constData());
    obs_data_set_string(os, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss");
    obs_data_set_string(os, "extension", "mkv");
    obs_data_set_int(os, "max_time_sec", qBound(5, s.i("recording.replayBufferSec"), 21600));
    obs_data_set_int(os, "max_size_mb", 2048);
    obs_output_update(m_replay.out, os);
    if (!m_streamVideoEnc) m_streamVideoEnc = makeVideoEncoder("Stream Video", 0);
    if (!m_streamAudioEnc) m_streamAudioEnc = makeAudioEncoder("Stream Audio", 0);
    obs_output_set_video_encoder(m_replay.out, m_streamVideoEnc);
    obs_output_set_audio_encoder(m_replay.out, m_streamAudioEnc, 0);
    connectOutputSignals(m_replay, "replay");
    if (!obs_output_start(m_replay.out)) {
        const char *le = obs_output_get_last_error(m_replay.out);
        if (error) *error = le ? QString::fromUtf8(le) : tr("Replay buffer failed to start.");
        m_replay.reset();
        return false;
    }
    Q_EMIT replayBufferStateChanged();
    return true;
}

void Engine::stopReplayBuffer() { if (isReplayBufferActive()) obs_output_stop(m_replay.out); }
bool Engine::isReplayBufferActive() const { return m_replay.out && obs_output_active(m_replay.out); }
bool Engine::saveReplay() {
    if (!isReplayBufferActive()) return false;
    calldata_t cd{};
    proc_handler_call(obs_output_get_proc_handler(m_replay.out), "save", &cd);
    calldata_free(&cd);
    return true;
}

bool Engine::virtualCamAvailable() const {
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_output_types(i, &id); ++i)
        if (strcmp(id, "virtualcam_output") == 0) return true;
    return false;
}

bool Engine::startVirtualCam(QString *error) {
    if (isVirtualCamActive()) return true;
    if (!virtualCamAvailable()) {
        if (error) *error = tr("Virtual camera component is not installed. Re-run the installer and tick "
                               "\"Virtual Camera\" (registers the DirectShow camera device).");
        return false;
    }
    m_vcam.out = obs_output_create("virtualcam_output", "HeartCast Virtual Camera", nullptr, nullptr);
    connectOutputSignals(m_vcam, "vcam");
    if (!m_vcam.out || !obs_output_start(m_vcam.out)) {
        const char *le = m_vcam.out ? obs_output_get_last_error(m_vcam.out) : nullptr;
        if (error) *error = le ? QString::fromUtf8(le)
                               : tr("Virtual camera failed to start. On Windows the camera driver must be registered (installer option).");
        m_vcam.reset();
        return false;
    }
    Q_EMIT virtualCamStateChanged();
    return true;
}
void Engine::stopVirtualCam() { if (isVirtualCamActive()) obs_output_stop(m_vcam.out); }
bool Engine::isVirtualCamActive() const { return m_vcam.out && obs_output_active(m_vcam.out); }

// ===================================================================
//  persistence
// ===================================================================
void Engine::clearAll() {
    // Never remove sources from inside obs_enum_*: the enumeration holds the
    // context lock that source destruction needs (deadlock). Collect refs first.
    if (m_transition) obs_transition_clear(m_transition);
    std::vector<obs_source_t *> owned;   // our per-scene ownership refs (looked up before removal)
    for (const QString &n : std::as_const(m_sceneOrder)) if (obs_source_t *s = sceneSource(n)) owned.push_back(s);
    struct Ctx { Engine *e; std::vector<obs_source_t *> list; } ctx{this, {}};
    auto collect = [](void *p, obs_source_t *src) {
        auto *c = static_cast<Ctx *>(p);
        if (src == c->e->m_desktopAudio.Get() || src == c->e->m_micAudio.Get()) return true;
        if (obs_source_t *ref = obs_source_get_ref(src)) c->list.push_back(ref);
        return true;
    };
    obs_enum_scenes(collect, &ctx);
    obs_enum_sources(collect, &ctx);
    for (obs_source_t *src : ctx.list) {
        if (obs_scene_t *sc = obs_scene_from_source(src)) {   // drop items so inputs lose their refs
            std::vector<obs_sceneitem_t *> items;
            obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *it, void *p) {
                obs_sceneitem_addref(it);
                static_cast<std::vector<obs_sceneitem_t *> *>(p)->push_back(it);
                return true;
            }, &items);
            for (auto *it : items) { obs_sceneitem_remove(it); obs_sceneitem_release(it); }
        }
        obs_source_remove(src);
    }
    for (obs_source_t *src : ctx.list) obs_source_release(src);
    for (obs_source_t *src : owned) obs_source_release(src);
    m_sceneOrder.clear();
    m_programScene.clear();
    m_previewScene.clear();
    obs_wait_for_destroy_queue();   // names/UUIDs are free again before we load new ones
}

bool Engine::saveCollection(const QString &path) const {
    struct Ctx { const Engine *e; obs_data_array_t *arr; } ctx{this, obs_data_array_create()};
    auto saver = [](void *p, obs_source_t *src) {
        auto *c = static_cast<Ctx *>(p);
        if (src == c->e->m_desktopAudio.Get() || src == c->e->m_micAudio.Get()) return true;
        if (obs_obj_is_private(src) || obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION) return true;
        if (obs_source_removed(src)) return true;
        OBSDataAutoRelease d = obs_save_source(src);
        obs_data_array_push_back(c->arr, d);
        return true;
    };
    obs_enum_scenes(saver, &ctx);
    obs_enum_sources(saver, &ctx);

    OBSDataAutoRelease root = obs_data_create();
    obs_data_set_array(root, "sources", ctx.arr);
    obs_data_array_release(ctx.arr);
    OBSDataArrayAutoRelease order = obs_data_array_create();
    for (const QString &n : m_sceneOrder) {
        OBSDataAutoRelease it = obs_data_create();
        obs_data_set_string(it, "name", n.toUtf8().constData());
        obs_data_array_push_back(order, it);
    }
    obs_data_set_array(root, "scene_order", order);
    obs_data_set_string(root, "current_scene", m_programScene.toUtf8().constData());
    obs_data_set_string(root, "transition", transitionId().toUtf8().constData());
    obs_data_set_int(root, "transition_ms", m_transitionMs);
    obs_data_set_string(root, "app", "HeartCast Studio");

    // Write via QSaveFile (atomic) so a crash mid-save never corrupts the collection.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(obs_data_get_json_pretty(root));
    return f.commit();
}

bool Engine::loadCollection(const QString &path) {
    OBSDataAutoRelease root = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
    if (!root) return false;
    OBSDataArrayAutoRelease sources = obs_data_get_array(root, "sources");
    if (!sources) return false;

    clearAll();
    // obs_load_sources releases its references when it returns, so scenes must be
    // ref'd inside the callback or they'd be destroyed immediately.
    QStringList loaded;
    obs_load_sources(sources, [](void *p, obs_source_t *src) {
        if (obs_source_is_scene(src) && !obs_source_is_group(src)) {
            (void)obs_source_get_ref(src);   // owned by m_sceneOrder; released in removeScene()/clearAll()
            static_cast<QStringList *>(p)->append(QString::fromUtf8(obs_source_get_name(src)));
        }
    }, &loaded);

    OBSDataArrayAutoRelease order = obs_data_get_array(root, "scene_order");
    for (size_t i = 0; order && i < obs_data_array_count(order); ++i) {
        OBSDataAutoRelease it = obs_data_array_item(order, i);
        const QString n = QString::fromUtf8(obs_data_get_string(it, "name"));
        if (loaded.contains(n) && !m_sceneOrder.contains(n)) m_sceneOrder << n;
    }
    for (const QString &n : loaded) if (!m_sceneOrder.contains(n)) m_sceneOrder << n;
    if (m_sceneOrder.isEmpty()) createScene(tr("Scene"));

    m_transitionMs = int(obs_data_get_int(root, "transition_ms"));
    if (m_transitionMs <= 0) m_transitionMs = 450;
    const QString cur = QString::fromUtf8(obs_data_get_string(root, "current_scene"));
    m_programScene.clear();
    transitionTo(m_sceneOrder.contains(cur) ? cur : m_sceneOrder.first());
    m_collectionPath = path;
    Settings::instance().set("general.lastCollection", path);
    Q_EMIT scenesChanged();
    return true;
}

bool Engine::loadStarterCollection() {
    QFile f(":/scenes/starter_collection.json");
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    clearAll();
    for (const auto &sv : root.value("scenes").toArray()) {
        const QJsonObject so = sv.toObject();
        const QString sceneName = createScene(so.value("name").toString());
        for (const auto &iv : so.value("items").toArray()) {
            const QJsonObject io = iv.toObject();
            const QString t = io.value("type").toString();
            const QJsonObject set = io.value("settings").toObject();
            OBSDataAutoRelease st = obs_data_create();
            QString typeId;
            if (t == "bc_gradient") {
                typeId = "bc_gradient";
                obs_data_set_string(st, "bc_c1", set.value("color1").toString().toUtf8().constData());
                obs_data_set_string(st, "bc_c2", set.value("color2").toString().toUtf8().constData());
            } else if (t == "text") {
#ifdef _WIN32
                typeId = "text_gdiplus";
#else
                typeId = "text_ft2_source_v2";
#endif
                obs_data_set_string(st, "text", set.value("text").toString().toUtf8().constData());
                OBSDataAutoRelease font = obs_data_create();
                obs_data_set_string(font, "face", "Nunito");
                obs_data_set_int(font, "size", set.value("size").toInt(72));
                obs_data_set_int(font, "flags", 1);   // bold
                obs_data_set_obj(st, "font", font);
                const QColor c(set.value("color").toString("#C96F9D"));
                const long long abgr = 0xFF000000LL | (c.blue() << 16) | (c.green() << 8) | c.red();
                obs_data_set_int(st, "color", abgr);
                obs_data_set_int(st, "color1", abgr);
                obs_data_set_int(st, "color2", abgr);
            } else if (t == "webcam") {
#ifdef _WIN32
                typeId = "dshow_input";
#else
                typeId = "v4l2_input";
#endif
            } else if (t == "game_capture") {
#ifdef _WIN32
                typeId = "game_capture";
#else
                typeId = "xcomposite_input";
#endif
            }
            if (typeId.isEmpty() || (typeId != "bc_gradient" && !obs_source_get_display_name(typeId.toUtf8().constData())))
                continue;   // that capture type doesn't exist on this OS — skip rather than fail
            obs_sceneitem_t *item = addSource(sceneName, typeId, io.value("name").toString(), st);
            if (!item) continue;
            const QString align = io.value("align").toString();
            const double scale = io.value("scale").toDouble(1.0);
            if (t == "text") {
                obs_video_info ovi{}; obs_get_video_info(&ovi);
                obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
                vec2 pos{ovi.base_width / 2.f, ovi.base_height / 2.f};
                obs_sceneitem_set_pos(item, &pos);
            } else if (t != "bc_gradient" || !align.isEmpty() || scale != 1.0) {
                fitItemToCanvas(item, scale, align.isEmpty() ? "center" : align);
            }
        }
    }
    if (!m_sceneOrder.isEmpty()) { m_programScene.clear(); transitionTo(m_sceneOrder.first()); }
    Q_EMIT scenesChanged();
    return true;
}

} // namespace bc
