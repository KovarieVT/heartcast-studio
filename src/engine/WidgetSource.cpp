#include "engine/WidgetSource.h"
#include <QMetaObject>
#include <util/platform.h>

namespace bc {

WidgetRegistry &WidgetRegistry::instance() {
    static WidgetRegistry r;
    return r;
}

QList<obs_source_t *> WidgetRegistry::sources() const {
    QMutexLocker l(&m_mutex);
    return m_widgets.keys();
}

WidgetConfig WidgetRegistry::config(obs_source_t *src) const {
    QMutexLocker l(&m_mutex);
    return m_widgets.value(src);
}

void WidgetRegistry::add(obs_source_t *src, const WidgetConfig &cfg) {
    { QMutexLocker l(&m_mutex); m_widgets.insert(src, cfg); }
    QMetaObject::invokeMethod(this, [this] { Q_EMIT widgetsChanged(); }, Qt::QueuedConnection);
}

void WidgetRegistry::update(obs_source_t *src, const WidgetConfig &cfg) {
    { QMutexLocker l(&m_mutex); if (!m_widgets.contains(src)) return; m_widgets[src] = cfg; }
    QMetaObject::invokeMethod(this, [this, src] { Q_EMIT widgetConfigChanged(src); }, Qt::QueuedConnection);
}

void WidgetRegistry::remove(obs_source_t *src) {
    { QMutexLocker l(&m_mutex); m_widgets.remove(src); }
    QMetaObject::invokeMethod(this, [this] { Q_EMIT widgetsChanged(); }, Qt::QueuedConnection);
}

void WidgetRegistry::pushFrame(obs_source_t *src, const QImage &in) {
    const QImage img = in.format() == QImage::Format_ARGB32 ? in : in.convertToFormat(QImage::Format_ARGB32);
    // Hold the lock for the whole push: the source's destroy callback (libobs destruction
    // thread) takes the same lock in remove(), so it cannot be freed while we hand it a frame.
    QMutexLocker l(&m_mutex);
    if (!m_widgets.contains(src)) return;   // already destroyed
    obs_source_frame f{};
    f.data[0] = const_cast<uint8_t *>(img.constBits());
    f.linesize[0] = uint32_t(img.bytesPerLine());
    f.width = uint32_t(img.width());
    f.height = uint32_t(img.height());
    f.format = VIDEO_FORMAT_BGRA;   // QImage ARGB32 on little-endian = B,G,R,A bytes
    f.timestamp = os_gettime_ns();
    obs_source_output_video(src, &f); // libobs copies the frame
}

WidgetConfig widgetConfigFromData(obs_data_t *s) {
    WidgetConfig c;
    c.kind = QString::fromUtf8(obs_data_get_string(s, "kind"));
    c.pack = QString::fromUtf8(obs_data_get_string(s, "pack"));
    c.width = int(obs_data_get_int(s, "width"));
    c.height = int(obs_data_get_int(s, "height"));
    c.title = QString::fromUtf8(obs_data_get_string(s, "title"));
    c.labelType = QString::fromUtf8(obs_data_get_string(s, "label_type"));
    c.goalMetric = QString::fromUtf8(obs_data_get_string(s, "goal_metric"));
    c.goalTarget = int(obs_data_get_int(s, "goal_target"));
    c.goalStart = int(obs_data_get_int(s, "goal_start"));
    c.countdownMinutes = int(obs_data_get_int(s, "countdown_minutes"));
    c.fontSize = int(obs_data_get_int(s, "font_size"));
    c.socials = QString::fromUtf8(obs_data_get_string(s, "socials"));
    c.soundFile = QString::fromUtf8(obs_data_get_string(s, "sound_file"));
    c.alertSeconds = int(obs_data_get_int(s, "alert_seconds"));
    return c;
}

// ------------------------------------------------------------------
//  obs_source_info callbacks
// ------------------------------------------------------------------
namespace {

const char *wName(void *) { return "HeartCast Widget"; }

void *wCreate(obs_data_t *settings, obs_source_t *source) {
    WidgetRegistry::instance().add(source, widgetConfigFromData(settings));
    return source;
}

void wDestroy(void *data) { WidgetRegistry::instance().remove(static_cast<obs_source_t *>(data)); }

void wUpdate(void *data, obs_data_t *settings) {
    WidgetRegistry::instance().update(static_cast<obs_source_t *>(data), widgetConfigFromData(settings));
}

void wDefaults(obs_data_t *s) {
    obs_data_set_default_string(s, "kind", "alerts");
    obs_data_set_default_string(s, "pack", "cotton-candy");
    obs_data_set_default_int(s, "width", 800);
    obs_data_set_default_int(s, "height", 240);
    obs_data_set_default_string(s, "title", "");
    obs_data_set_default_string(s, "label_type", "recent_follower");
    obs_data_set_default_string(s, "goal_metric", "followers");
    obs_data_set_default_int(s, "goal_target", 100);
    obs_data_set_default_int(s, "countdown_minutes", 10);
    obs_data_set_default_int(s, "font_size", 28);
    obs_data_set_default_int(s, "alert_seconds", 6);
}

bool kindModified(obs_properties_t *props, obs_property_t *, obs_data_t *s) {
    const QString k = QString::fromUtf8(obs_data_get_string(s, "kind"));
    obs_property_set_visible(obs_properties_get(props, "label_type"), k == "label");
    obs_property_set_visible(obs_properties_get(props, "goal_metric"), k == "goal");
    obs_property_set_visible(obs_properties_get(props, "goal_target"), k == "goal");
    obs_property_set_visible(obs_properties_get(props, "goal_start"), k == "goal");
    obs_property_set_visible(obs_properties_get(props, "countdown_minutes"), k == "countdown");
    obs_property_set_visible(obs_properties_get(props, "socials"), k == "social" || k == "sponsor");
    obs_property_set_visible(obs_properties_get(props, "sound_file"), k == "alerts");
    obs_property_set_visible(obs_properties_get(props, "alert_seconds"), k == "alerts");
    return true;
}

obs_properties_t *wProperties(void *) {
    obs_properties_t *p = obs_properties_create();
    obs_property_t *kind = obs_properties_add_list(p, "kind", "Widget", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *kinds[][2] = {{"Alerts", "alerts"}, {"Chat box", "chat"}, {"Event list", "events"},
                              {"Stream label", "label"}, {"Goal bar", "goal"}, {"Countdown", "countdown"},
                              {"Stopwatch", "stopwatch"}, {"Social panel", "social"}, {"Sponsor panel", "sponsor"},
                              {"Viewer count", "viewers"}, {"Text panel", "text_panel"}};
    for (auto &k : kinds) obs_property_list_add_string(kind, k[0], k[1]);
    obs_property_set_modified_callback(kind, kindModified);

    obs_property_t *pack = obs_properties_add_list(p, "pack", "Overlay pack", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *packs[][2] = {{"Cotton Candy", "cotton-candy"}, {"Strawberry Milk", "strawberry-milk"},
                              {"Mint Macaron", "mint-macaron"}, {"Pastel Dream", "pastel-dream"},
                              {"Pink Galaxy", "pink-galaxy"}, {"Cherry Blossom", "cherry-blossom"},
                              {"Magical Girl", "magical-girl"}, {"Soft Academia", "soft-academia"}};
    for (auto &k : packs) obs_property_list_add_string(pack, k[0], k[1]);

    obs_properties_add_int(p, "width", "Width", 64, 3840, 1);
    obs_properties_add_int(p, "height", "Height", 32, 2160, 1);
    obs_properties_add_text(p, "title", "Title / text", OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "font_size", "Font size", 8, 200, 1);

    obs_property_t *lt = obs_properties_add_list(p, "label_type", "Label shows", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(lt, "Most recent follower", "recent_follower");
    obs_property_list_add_string(lt, "Most recent subscriber", "recent_sub");
    obs_property_list_add_string(lt, "Most recent tip/cheer", "recent_donation");
    obs_property_list_add_string(lt, "Session followers", "session_followers");

    obs_property_t *gm = obs_properties_add_list(p, "goal_metric", "Goal counts", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(gm, "New followers this stream", "followers");
    obs_property_list_add_string(gm, "New subscribers this stream", "subs");
    obs_property_list_add_string(gm, "Bits / tips this stream", "donations");
    obs_properties_add_int(p, "goal_target", "Goal target", 1, 10000000, 1);
    obs_properties_add_int(p, "goal_start", "Starting value", 0, 10000000, 1);
    obs_properties_add_int(p, "countdown_minutes", "Countdown minutes", 1, 1440, 1);
    obs_properties_add_text(p, "socials", "Handles (platform:name; …)", OBS_TEXT_DEFAULT);
    obs_properties_add_path(p, "sound_file", "Alert sound", OBS_PATH_FILE, "Audio (*.wav *.mp3 *.ogg)", nullptr);
    obs_properties_add_int(p, "alert_seconds", "Alert duration (s)", 2, 30, 1);
    return p;
}

obs_source_info g_info{};
bool g_registered = false;

} // namespace

void registerWidgetSource() {
    if (g_registered) return;
    g_info.id = "bc_widget";
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    g_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    g_info.get_name = wName;
    g_info.create = wCreate;
    g_info.destroy = wDestroy;
    g_info.update = wUpdate;
    g_info.get_defaults = wDefaults;
    g_info.get_properties = wProperties;
    g_info.icon_type = OBS_ICON_TYPE_CUSTOM;
    obs_register_source(&g_info);
    g_registered = true;
}

void unregisterWidgetSources() { g_registered = false; /* libobs frees registrations on obs_shutdown */ }

} // namespace bc
