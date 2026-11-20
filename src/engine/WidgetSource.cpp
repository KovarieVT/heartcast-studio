#include "engine/WidgetSource.h"
#include <QJsonDocument>
#include "overlays/WidgetCatalog.h"
#include "overlays/WidgetManager.h"
#include <QHash>
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
    auto str = [s](const char *k) { return QString::fromUtf8(obs_data_get_string(s, k)); };
    auto num = [s](const char *k) { return int(obs_data_get_int(s, k)); };
    WidgetConfig c;
    c.kind = str("kind");
    c.pack = str("pack");
    c.width = num("width");
    c.height = num("height");
    c.title = str("title");
    c.text = str("text");
    c.labelType = str("label_type");
    c.goalMetric = str("goal_metric");
    c.metric = str("metric");
    c.keyword = str("keyword");
    c.target = str("target");
    c.file = str("file");
    c.audioSource = str("audio_source");
    c.goalTarget = num("goal_target");
    c.goalStart = num("goal_start");
    c.countdownMinutes = num("countdown_minutes");
    c.count = num("count");
    c.interval = num("interval");
    c.value = num("value");
    c.value2 = num("value2");
    c.addFollow = num("add_follow");
    c.addSub = num("add_sub");
    c.addTip = num("add_tip");
    c.subathonEnd = obs_data_get_int(s, "subathon_end");
    c.fontSize = num("font_size");
    c.socials = str("socials");
    c.soundFile = str("sound_file");
    c.alertSeconds = num("alert_seconds");
    if (c.file.isEmpty()) c.file = str("folder");   // slideshow stores its folder separately
    return c;
}

// ------------------------------------------------------------------
//  obs_source_info callbacks
// ------------------------------------------------------------------
namespace {

const char *wName(void *) { return "KovaLive Widget"; }

void *wCreate(obs_data_t *settings, obs_source_t *source) {
    WidgetRegistry::instance().add(source, widgetConfigFromData(settings));
    return source;
}

void wDestroy(void *data) { WidgetRegistry::instance().remove(static_cast<obs_source_t *>(data)); }

void wUpdate(void *data, obs_data_t *settings) {
    WidgetRegistry::instance().update(static_cast<obs_source_t *>(data), widgetConfigFromData(settings));
}

void wDefaults(obs_data_t *s);

} // namespace

WidgetConfig widgetConfigFromJson(const QJsonObject &values) {
    OBSDataAutoRelease st = obs_data_create_from_json(QJsonDocument(values).toJson(QJsonDocument::Compact).constData());
    wDefaults(st);
    return widgetConfigFromData(st);
}

namespace {

void wDefaults(obs_data_t *s) {
    obs_data_set_default_string(s, "kind", "alerts");
    obs_data_set_default_string(s, "pack", "nocturne");
    obs_data_set_default_int(s, "width", 800);
    obs_data_set_default_int(s, "height", 240);
    obs_data_set_default_string(s, "title", "");
    obs_data_set_default_string(s, "label_type", "recent_follower");
    obs_data_set_default_string(s, "goal_metric", "followers");
    obs_data_set_default_string(s, "metric", "all");
    obs_data_set_default_int(s, "goal_target", 100);
    obs_data_set_default_int(s, "countdown_minutes", 10);
    obs_data_set_default_int(s, "count", 5);
    obs_data_set_default_int(s, "interval", 10);
    obs_data_set_default_int(s, "add_follow", 30);
    obs_data_set_default_int(s, "add_sub", 120);
    obs_data_set_default_int(s, "add_tip", 60);
    obs_data_set_default_int(s, "font_size", 0);   // 0 = automatic
    obs_data_set_default_int(s, "alert_seconds", 6);
}

// Which settings each widget kind uses (everything else is hidden in its properties).
const QHash<QString, QStringList> &kindFields() {
    static const QHash<QString, QStringList> f = {
        {"alerts", {"sound_file", "alert_seconds"}},
        {"events", {"count"}},
        {"ticker", {"metric", "count"}},
        {"recent_list", {"title", "metric", "count"}},
        {"raid_banner", {"title"}},
        {"hype_meter", {"title", "goal_target"}},
        {"combo", {"title"}},
        {"label", {"title", "label_type"}},
        {"goal", {"title", "goal_metric", "goal_target", "goal_start"}},
        {"goal_ring", {"title", "goal_metric", "goal_target", "goal_start"}},
        {"goal_candle", {"title", "goal_metric", "goal_target", "goal_start"}},
        {"chat", {}},
        {"chat_bubble", {}},
        {"chat_marquee", {}},
        {"poll", {"title", "text", "keyword", "btn_poll_reset"}},
        {"giveaway", {"title", "keyword", "btn_draw", "btn_entries_clear"}},
        {"qna", {"title", "keyword", "btn_qna_next", "btn_qna_clear"}},
        {"word_counter", {"title", "keyword", "btn_words_reset"}},
        {"first_chatter", {"title"}},
        {"leaderboard", {"title", "metric", "count"}},
        {"counter", {"title", "keyword", "value", "btn_inc", "btn_dec", "btn_reset"}},
        {"winloss", {"title", "value", "value2", "btn_win", "btn_loss", "btn_reset"}},
        {"clock", {"title"}},
        {"analog_clock", {}},
        {"date", {}},
        {"world_clock", {"title", "target"}},
        {"countdown", {"title", "countdown_minutes", "btn_restart"}},
        {"countdown_date", {"title", "target"}},
        {"stopwatch", {"title"}},
        {"subathon", {"title", "countdown_minutes", "add_follow", "add_sub", "add_tip", "btn_restart", "btn_add5"}},
        {"pomodoro", {"countdown_minutes", "interval", "btn_restart"}},
        {"moon_phase", {}},
        {"viewers", {}},
        {"on_air", {"title"}},
        {"rec_indicator", {}},
        {"stream_health", {}},
        {"bitrate_graph", {}},
        {"fps_meter", {}},
        {"cpu_meter", {}},
        {"scene_name", {"title"}},
        {"voice_sigil", {"audio_source"}},
        {"audio_visualizer", {"audio_source", "count"}},
        {"social", {"socials"}},
        {"sponsor", {"socials"}},
        {"text_panel", {"title"}},
        {"schedule", {"title", "text"}},
        {"rotator", {"text", "interval"}},
        {"lower_third", {"title", "text"}},
        {"banner", {"title"}},
        {"file_text", {"title", "file"}},
        {"slideshow", {"folder", "interval"}},
        {"reminder", {"title", "interval"}},
        {"ambient", {"metric", "count"}},
        {"candles", {"count"}},
        {"webcam_frame", {}},
        {"divider", {}},
    };
    return f;
}

bool kindModified(obs_properties_t *props, obs_property_t *, obs_data_t *s) {
    const QString k = QString::fromUtf8(obs_data_get_string(s, "kind"));
    const QStringList used = kindFields().value(k);
    for (const char *name : {"title", "text", "label_type", "goal_metric", "metric", "keyword", "target", "file", "folder", "audio_source",
                             "goal_target", "goal_start", "countdown_minutes", "count", "interval", "value", "value2", "add_follow", "add_sub",
                             "add_tip", "socials", "sound_file", "alert_seconds", "btn_inc", "btn_dec", "btn_reset", "btn_win", "btn_loss",
                             "btn_restart", "btn_add5", "btn_draw", "btn_entries_clear", "btn_poll_reset", "btn_qna_next", "btn_qna_clear",
                             "btn_words_reset"})
        if (obs_property_t *pr = obs_properties_get(props, name)) obs_property_set_visible(pr, used.contains(QLatin1String(name)));

    // per-kind wording and choices
    auto describe = [&](const char *name, const char *text) { if (obs_property_t *pr = obs_properties_get(props, name)) obs_property_set_description(pr, text); };
    describe("title", k == "lower_third" ? "Name" : k == "banner" ? "Scrolling text" : k == "file_text" ? "Prefix (e.g. Now playing)"
                    : k == "reminder" ? "Reminder text" : "Title / text");
    describe("text", k == "poll" ? "Options (one per line)" : k == "lower_third" ? "Subtitle" : k == "rotator" ? "Messages (one per line)" : "Lines (one per line)");
    describe("target", k == "world_clock" ? "Time zone (e.g. Europe/London, America/New_York)" : "Date and time (YYYY-MM-DD HH:MM)");
    describe("interval", k == "reminder" ? "Remind every (minutes)" : k == "pomodoro" ? "Break length (minutes)" : "Seconds per item");
    describe("count", k == "audio_visualizer" ? "Bars" : k == "candles" ? "Candles" : k == "ambient" ? "Amount" : "Items to show");
    describe("countdown_minutes", k == "pomodoro" ? "Focus length (minutes)" : k == "subathon" ? "Starting time (minutes)" : "Countdown minutes");
    describe("keyword", k == "word_counter" ? "Word or emote to count" : k == "counter" ? "Mod chat command (+1; add a minus for -1, e.g. !death-)"
                      : "Chat command");
    describe("value", k == "winloss" ? "Wins" : "Count");

    if (obs_property_t *m = obs_properties_get(props, "metric")) {
        obs_property_list_clear(m);
        auto opt = [m](const char *label, const char *v) { obs_property_list_add_string(m, label, v); };
        if (k == "ticker" || k == "recent_list") {
            opt("Followers", "follow"); opt("Subscribers", "subscribe"); opt("Tips & cheers", "tips"); opt("Gifted subs", "gift");
            opt("Raids", "raid"); opt("Members", "membership"); if (k == "ticker") opt("Everything", "all");
        } else if (k == "leaderboard") {
            opt("Top gifters", "gifters"); opt("Top tippers", "tippers"); opt("Top chatters", "chatters");
        } else if (k == "ambient") {
            opt("Bats", "bats"); opt("Embers", "embers"); opt("Rose petals", "petals"); opt("Fog", "fog"); opt("Falling ash", "ash"); opt("Feathers", "feathers");
        }
    }
    return true;
}

// ---- buttons: called on the UI thread by the properties window; `data` is the source.
// Returning true makes the window refresh so changed values (e.g. a counter) show at once.
template <void (*Fn)(obs_source_t *)> bool button(obs_properties_t *, obs_property_t *, void *data) {
    Fn(static_cast<obs_source_t *>(data));
    return true;
}
void doInc(obs_source_t *s) { WidgetManager::instance().adjustCounter(s, +1, 0); }
void doDec(obs_source_t *s) { WidgetManager::instance().adjustCounter(s, -1, 0); }
void doReset(obs_source_t *s) { WidgetManager::instance().resetCounter(s); }
void doWin(obs_source_t *s) { WidgetManager::instance().adjustCounter(s, +1, 0); }
void doLoss(obs_source_t *s) { WidgetManager::instance().adjustCounter(s, 0, +1); }
void doRestart(obs_source_t *s) { WidgetManager::instance().restartWidget(s); }
void doAdd5(obs_source_t *s) { WidgetManager::instance().addSubathonSeconds(s, 300); }
void doDraw(obs_source_t *s) { WidgetManager::instance().drawGiveawayWinner(s); }
void doClearEntries(obs_source_t *s) { WidgetManager::instance().clearChatState(s); }
void doPollReset(obs_source_t *s) { WidgetManager::instance().clearChatState(s); }
void doQnaNext(obs_source_t *s) { WidgetManager::instance().nextQuestion(s); }
void doQnaClear(obs_source_t *s) { WidgetManager::instance().clearChatState(s); }
void doWordsReset(obs_source_t *s) { WidgetManager::instance().clearChatState(s); }

obs_properties_t *wProperties(void *) {
    obs_properties_t *p = obs_properties_create();
    obs_property_t *kind = obs_properties_add_list(p, "kind", "Widget", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const QString &k : widgetKinds())
        obs_property_list_add_string(kind, widgetKindName(k).toUtf8().constData(), k.toUtf8().constData());
    obs_property_set_modified_callback(kind, kindModified);

    obs_property_t *pack = obs_properties_add_list(p, "pack", "Overlay pack", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const auto &pk : WidgetManager::instance().packs())
        obs_property_list_add_string(pack, pk.name.toUtf8().constData(), pk.id.toUtf8().constData());

    obs_properties_add_int(p, "width", "Width", 64, 3840, 1);
    obs_properties_add_int(p, "height", "Height", 32, 2160, 1);
    obs_properties_add_int(p, "font_size", "Font size (0 = automatic)", 0, 200, 1);
    obs_properties_add_text(p, "title", "Title / text", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "text", "Lines (one per line)", OBS_TEXT_MULTILINE);

    obs_property_t *lt = obs_properties_add_list(p, "label_type", "Label shows", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *labels[][2] = {{"Latest follower", "recent_follower"}, {"Latest subscriber", "recent_sub"}, {"Latest gifter", "recent_gift"},
                               {"Latest raider", "recent_raid"}, {"Latest cheer", "recent_cheer"}, {"Latest tip", "recent_donation"},
                               {"Latest member", "recent_member"}, {"Latest redemption", "recent_redemption"},
                               {"Followers this stream", "session_followers"}, {"Subs this stream", "session_subs"},
                               {"Bits this stream", "session_bits"}, {"Tips this stream", "session_tips"}, {"Chatters this stream", "session_chatters"}};
    for (auto &l : labels) obs_property_list_add_string(lt, l[0], l[1]);

    obs_property_t *gm = obs_properties_add_list(p, "goal_metric", "Goal counts", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(gm, "New followers this stream", "followers");
    obs_property_list_add_string(gm, "New subscribers & gifts this stream", "subs");
    obs_property_list_add_string(gm, "Tips this stream", "donations");
    obs_property_list_add_string(gm, "Bits this stream", "bits");
    obs_property_list_add_string(gm, "Chat messages this stream", "messages");
    obs_properties_add_list(p, "metric", "Shows", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_properties_add_int(p, "goal_target", "Goal target", 1, 10000000, 1);
    obs_properties_add_int(p, "goal_start", "Starting value", 0, 10000000, 1);
    obs_properties_add_text(p, "keyword", "Chat command", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "target", "Date and time", OBS_TEXT_DEFAULT);
    obs_properties_add_path(p, "file", "Text file", OBS_PATH_FILE, "Text (*.txt);;All files (*.*)", nullptr);
    obs_properties_add_path(p, "folder", "Image folder", OBS_PATH_DIRECTORY, nullptr, nullptr);
    obs_property_t *aud = obs_properties_add_list(p, "audio_source", "Audio source", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(aud, "First microphone", "");
    obs_enum_sources([](void *list, obs_source_t *src) {
        if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)
            obs_property_list_add_string(static_cast<obs_property_t *>(list), obs_source_get_name(src), obs_source_get_name(src));
        return true;
    }, aud);
    obs_properties_add_int(p, "countdown_minutes", "Countdown minutes", 1, 10080, 1);
    obs_properties_add_int(p, "count", "Items to show", 1, 64, 1);
    obs_properties_add_int(p, "interval", "Seconds per item", 1, 1440, 1);
    obs_properties_add_int(p, "value", "Count", -100000, 1000000, 1);
    obs_properties_add_int(p, "value2", "Losses", 0, 1000000, 1);
    obs_properties_add_int(p, "add_follow", "Seconds added per follow", 0, 3600, 5);
    obs_properties_add_int(p, "add_sub", "Seconds added per sub / gifted sub", 0, 3600, 5);
    obs_properties_add_int(p, "add_tip", "Seconds added per 1 tipped / 100 bits", 0, 3600, 5);
    obs_properties_add_text(p, "socials", "Handles (platform:name; …)", OBS_TEXT_DEFAULT);
    obs_properties_add_path(p, "sound_file", "Alert sound", OBS_PATH_FILE, "Audio (*.wav *.mp3 *.ogg)", nullptr);
    obs_properties_add_int(p, "alert_seconds", "Alert duration (s)", 2, 30, 1);

    obs_properties_add_button(p, "btn_inc", "+1", button<doInc>);
    obs_properties_add_button(p, "btn_dec", "−1", button<doDec>);
    obs_properties_add_button(p, "btn_win", "Win +1", button<doWin>);
    obs_properties_add_button(p, "btn_loss", "Loss +1", button<doLoss>);
    obs_properties_add_button(p, "btn_reset", "Reset to zero", button<doReset>);
    obs_properties_add_button(p, "btn_restart", "Restart timer", button<doRestart>);
    obs_properties_add_button(p, "btn_add5", "Add 5 minutes", button<doAdd5>);
    obs_properties_add_button(p, "btn_draw", "Draw a winner", button<doDraw>);
    obs_properties_add_button(p, "btn_entries_clear", "Clear entries", button<doClearEntries>);
    obs_properties_add_button(p, "btn_poll_reset", "Reset votes", button<doPollReset>);
    obs_properties_add_button(p, "btn_qna_next", "Next question", button<doQnaNext>);
    obs_properties_add_button(p, "btn_qna_clear", "Clear questions", button<doQnaClear>);
    obs_properties_add_button(p, "btn_words_reset", "Reset count", button<doWordsReset>);
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
