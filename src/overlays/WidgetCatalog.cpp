#include "overlays/WidgetCatalog.h"
#include <QCoreApplication>
#include <QHash>

namespace bc {

static QString T(const char *s) { return QCoreApplication::translate("WidgetCatalog", s); }

const QVector<WidgetCatalogEntry> &widgetCatalog() {
    static QVector<WidgetCatalogEntry> c;
    if (!c.isEmpty()) return c;
    auto add = [](const QString &id, const QString &name, const QString &cat, const QString &desc, const QString &kind,
                  QJsonObject defs, QSize size) {
        defs.insert("kind", kind);
        c.push_back({id, name, cat, desc, kind, defs, size});
    };
    const QString ev = T("Alerts & Events"), lb = T("Labels"), gl = T("Goals"), ch = T("Chat"), ld = T("Leaderboards"),
                  ct = T("Counters"), tm = T("Time"), st = T("Stream Stats"), in = T("Info Panels"), am = T("Ambience & Decor");

    // ---- alerts & events
    add("alerts", T("Alert box"), ev, T("Animated alerts for follows, subs, gifts, raids, cheers and tips, with sound."), "alerts", {}, {800, 240});
    add("events", T("Event list"), ev, T("The latest events, newest first."), "events", {{"count", 8}}, {480, 420});
    add("ticker.follow", T("Follower ticker"), ev, T("Recent followers scrolling across a bar."), "ticker", {{"metric", "follow"}}, {1280, 70});
    add("ticker.subscribe", T("Subscriber ticker"), ev, T("Recent subscribers scrolling across a bar."), "ticker", {{"metric", "subscribe"}}, {1280, 70});
    add("ticker.tips", T("Tip & cheer ticker"), ev, T("Recent tips and cheers scrolling across a bar."), "ticker", {{"metric", "tips"}}, {1280, 70});
    add("ticker.all", T("Event ticker"), ev, T("Every recent event scrolling across a bar."), "ticker", {{"metric", "all"}}, {1280, 70});
    add("recent.follow", T("Recent followers"), ev, T("The last few followers."), "recent_list", {{"metric", "follow"}, {"count", 5}, {"title", T("Recent followers")}}, {420, 320});
    add("recent.subscribe", T("Recent subscribers"), ev, T("The last few subscribers."), "recent_list", {{"metric", "subscribe"}, {"count", 5}, {"title", T("Recent subscribers")}}, {420, 320});
    add("recent.tips", T("Recent tippers"), ev, T("The last few tips and cheers with amounts."), "recent_list", {{"metric", "tips"}, {"count", 5}, {"title", T("Recent tips")}}, {420, 320});
    add("raid_banner", T("Raid banner"), ev, T("The latest raider and the size of their raid."), "raid_banner", {}, {900, 180});
    add("hype_meter", T("Hype meter"), ev, T("Fills with every follow, sub, gift, cheer and chat message in the last 5 minutes."), "hype_meter", {{"goal_target", 50}}, {800, 150});
    add("combo", T("Sub combo"), ev, T("Counts subs and gifts that arrive within a minute of each other."), "combo", {}, {420, 200});

    // ---- labels
    struct L { const char *id, *name, *type; };
    const L labels[] = {{"latest_follower", "Latest follower", "recent_follower"}, {"latest_sub", "Latest subscriber", "recent_sub"},
                        {"latest_gift", "Latest gifter", "recent_gift"},       {"latest_raid", "Latest raider", "recent_raid"},
                        {"latest_cheer", "Latest cheer", "recent_cheer"},      {"latest_tip", "Latest tip", "recent_donation"},
                        {"latest_member", "Latest member", "recent_member"},   {"latest_redemption", "Latest redemption", "recent_redemption"},
                        {"session_followers", "Followers this stream", "session_followers"}, {"session_subs", "Subs this stream", "session_subs"},
                        {"session_bits", "Bits this stream", "session_bits"}, {"session_tips", "Tips this stream", "session_tips"},
                        {"session_chatters", "Chatters this stream", "session_chatters"}};
    for (const auto &l : labels)
        add(QStringLiteral("label.") + l.id, T(l.name), lb, T("A single line that updates live."), "label", {{"label_type", l.type}}, {720, 120});

    // ---- goals
    add("goal.followers", T("Follower goal"), gl, T("Progress bar toward a follower goal."), "goal", {{"goal_metric", "followers"}, {"goal_target", 50}, {"title", T("Follower goal")}}, {800, 140});
    add("goal.subs", T("Sub goal"), gl, T("Progress bar toward a subscriber goal."), "goal", {{"goal_metric", "subs"}, {"goal_target", 25}, {"title", T("Sub goal")}}, {800, 140});
    add("goal.tips", T("Tip goal"), gl, T("Progress bar toward a tip goal."), "goal", {{"goal_metric", "donations"}, {"goal_target", 100}, {"title", T("Tip goal")}}, {800, 140});
    add("goal.bits", T("Bits goal"), gl, T("Progress bar toward a bits goal."), "goal", {{"goal_metric", "bits"}, {"goal_target", 5000}, {"title", T("Bits goal")}}, {800, 140});
    add("goal.chat", T("Chat goal"), gl, T("Progress toward a number of chat messages."), "goal", {{"goal_metric", "messages"}, {"goal_target", 500}, {"title", T("Chat goal")}}, {800, 140});
    add("goal_ring", T("Ring goal"), gl, T("A circular goal gauge."), "goal_ring", {{"goal_metric", "followers"}, {"goal_target", 50}, {"title", T("Followers")}}, {320, 320});
    add("goal_candle", T("Candle goal"), gl, T("A candle that burns down as you approach the goal."), "goal_candle", {{"goal_metric", "subs"}, {"goal_target", 20}, {"title", T("Subs")}}, {260, 460});

    // ---- chat
    add("chat", T("Chat box"), ch, T("Live chat from every connected platform."), "chat", {}, {520, 680});
    add("chat_bubble", T("Chat bubble"), ch, T("The latest chat message, large."), "chat_bubble", {}, {720, 200});
    add("chat_marquee", T("Chat marquee"), ch, T("Chat scrolling across a single line."), "chat_marquee", {}, {1280, 70});
    add("poll", T("Chat poll"), ch, T("Viewers vote by typing !vote 1, !vote 2… Results update live."), "poll",
        {{"title", T("What should we play next?")}, {"text", T("Option one\nOption two\nOption three")}, {"keyword", "!vote"}}, {640, 360});
    add("giveaway", T("Giveaway"), ch, T("Viewers enter with !join; draw a winner from the widget's settings or a hotkey."), "giveaway",
        {{"title", T("Giveaway")}, {"keyword", "!join"}}, {560, 240});
    add("qna", T("Q&A board"), ch, T("Shows questions viewers send with !q; step through them with a button or hotkey."), "qna", {{"keyword", "!q"}}, {720, 220});
    add("word_counter", T("Word counter"), ch, T("Counts how often a word or emote appears in chat."), "word_counter", {{"keyword", "LUL"}, {"title", T("LUL count")}}, {420, 160});
    add("first_chatter", T("First chatter"), ch, T("Celebrates the first person to chat this stream."), "first_chatter", {{"title", T("First chatter")}}, {620, 140});

    // ---- leaderboards
    add("leaderboard.gifters", T("Top gifters"), ld, T("Who gifted the most subs this stream."), "leaderboard", {{"metric", "gifters"}, {"count", 5}, {"title", T("Top gifters")}}, {440, 360});
    add("leaderboard.tippers", T("Top tippers"), ld, T("Biggest tips and cheers this stream."), "leaderboard", {{"metric", "tippers"}, {"count", 5}, {"title", T("Top tippers")}}, {440, 360});
    add("leaderboard.chatters", T("Top chatters"), ld, T("Most active chatters this stream."), "leaderboard", {{"metric", "chatters"}, {"count", 5}, {"title", T("Top chatters")}}, {440, 360});

    // ---- counters
    add("counter.deaths", T("Death counter"), ct, T("+1 / −1 from the widget settings, a hotkey, or mods typing !death."), "counter", {{"title", T("Deaths")}, {"keyword", "!death"}}, {420, 160});
    add("counter.kills", T("Kill counter"), ct, T("+1 / −1 from the widget settings, a hotkey, or mods typing !kill."), "counter", {{"title", T("Kills")}, {"keyword", "!kill"}}, {420, 160});
    add("winloss", T("Win / loss tracker"), ct, T("Wins and losses with buttons, hotkeys or mod commands !win / !loss."), "winloss", {{"title", T("Today")}}, {520, 160});

    // ---- time
    add("clock", T("Clock"), tm, T("Your local time."), "clock", {}, {420, 140});
    add("analog_clock", T("Gothic clock"), tm, T("An ornate analog clock face."), "analog_clock", {}, {360, 360});
    add("date", T("Date"), tm, T("Today's date."), "date", {}, {560, 120});
    add("world_clock", T("World clock"), tm, T("The time in another time zone."), "world_clock", {{"target", "Europe/London"}}, {460, 140});
    add("countdown", T("Countdown"), tm, T("Counts down from a number of minutes."), "countdown", {{"countdown_minutes", 10}}, {480, 140});
    add("countdown.starting", T("Starting soon"), tm, T("A starting-soon card with a countdown."), "countdown", {{"countdown_minutes", 5}, {"title", T("Starting soon")}}, {900, 200});
    add("countdown_date", T("Countdown to date"), tm, T("Days, hours and minutes until a date you choose."), "countdown_date", {{"target", "2026-12-31 23:59"}, {"title", T("New Year")}}, {640, 160});
    add("stopwatch", T("Stream uptime"), tm, T("How long you've been live."), "stopwatch", {}, {420, 140});
    add("subathon", T("Subathon timer"), tm, T("A countdown that gains time with every follow, sub and tip."), "subathon", {{"countdown_minutes", 120}, {"title", T("Subathon")}}, {640, 180});
    add("pomodoro", T("Pomodoro timer"), tm, T("Focus and break cycles for co-working streams."), "pomodoro", {{"countdown_minutes", 25}, {"interval", 5}}, {420, 220});
    add("moon_phase", T("Moon phase"), tm, T("Tonight's real moon phase."), "moon_phase", {}, {360, 260});

    // ---- stream stats
    add("viewers", T("Viewer count"), st, T("Viewers across all connected platforms."), "viewers", {}, {420, 120});
    add("on_air", T("ON AIR sign"), st, T("Lights up while you're live."), "on_air", {}, {360, 120});
    add("rec_indicator", T("Recording indicator"), st, T("A REC light with the recording time."), "rec_indicator", {}, {300, 90});
    add("stream_health", T("Stream health"), st, T("Bitrate, dropped frames and FPS at a glance."), "stream_health", {}, {520, 170});
    add("bitrate_graph", T("Bitrate graph"), st, T("Upload bitrate over the last minute."), "bitrate_graph", {}, {520, 200});
    add("fps_meter", T("FPS meter"), st, T("Frames per second the encoder is producing."), "fps_meter", {}, {300, 120});
    add("cpu_meter", T("CPU meter"), st, T("This PC's CPU usage."), "cpu_meter", {}, {300, 120});
    add("scene_name", T("Current scene"), st, T("The name of the scene on air."), "scene_name", {}, {560, 110});
    add("voice_sigil", T("Voice sigil"), st, T("A rose-window sigil that glows when you talk."), "voice_sigil", {}, {300, 300});
    add("audio_visualizer", T("Audio visualizer"), st, T("A live spectrum of your microphone or desktop audio."), "audio_visualizer", {{"count", 32}}, {720, 200});

    // ---- info panels
    add("social", T("Social panel"), in, T("Your handles on other platforms."), "social", {{"socials", "twitch:yourname;tiktok:yourname"}}, {520, 260});
    add("sponsor", T("Sponsor panel"), in, T("Sponsors and partners."), "sponsor", {{"socials", ":Your sponsor"}}, {520, 200});
    add("text_panel", T("Text panel"), in, T("Any text you like."), "text_panel", {{"title", T("Welcome to the night")}}, {800, 200});
    add("schedule", T("Schedule"), in, T("Your weekly stream schedule."), "schedule",
        {{"title", T("Schedule")}, {"text", T("Mon — 8 PM\nWed — 8 PM\nFri — 9 PM\nSun — Late night")}}, {460, 360});
    add("rotator", T("Rotating messages"), in, T("Cycles through messages: rules, tips, links."), "rotator",
        {{"text", T("Follow for more\nBe kind in chat\nCommands: !socials !schedule")}, {"interval", 8}}, {900, 120});
    add("rules", T("Chat rules"), in, T("Your chat rules as a numbered list."), "schedule",
        {{"title", T("Rules")}, {"text", T("Be kind\nNo spoilers\nNo self-promo\nHave fun")}}, {460, 340});
    add("lower_third", T("Lower third"), in, T("Name and subtitle that slide in, like broadcast TV."), "lower_third",
        {{"title", T("Your name")}, {"text", T("Streamer · Night owl")}}, {900, 160});
    add("banner", T("Scrolling banner"), in, T("Your own text scrolling across a bar."), "banner", {{"title", T("Thanks for watching — new streams every week")}}, {1280, 70});
    add("now_playing", T("Now playing"), in, T("Song info from a text file your music app writes (e.g. Tuna, Snip)."), "file_text", {{"title", T("Now playing")}}, {720, 120});
    add("file_text", T("Text from file"), in, T("Shows the contents of any text file and updates when it changes."), "file_text", {}, {720, 120});
    add("slideshow", T("Image slideshow"), in, T("Cycles through the pictures in a folder with a crossfade."), "slideshow", {{"interval", 8}}, {640, 360});
    add("reminder.water", T("Hydration reminder"), in, T("Pops up every so often to remind you to drink water."), "reminder", {{"title", T("Drink some water")}, {"interval", 30}}, {560, 140});
    add("reminder.stretch", T("Stretch reminder"), in, T("Pops up every so often to remind you to stretch."), "reminder", {{"title", T("Time to stretch")}, {"interval", 60}}, {560, 140});

    // ---- ambience & decor
    struct A { const char *id, *name, *desc; };
    const A amb[] = {{"bats", "Bats", "Bats flitting across the screen."}, {"embers", "Embers", "Glowing embers rising."},
                     {"petals", "Rose petals", "Rose petals drifting down."}, {"fog", "Fog", "Slow fog rolling along the bottom."},
                     {"ash", "Falling ash", "Ash drifting down like snow."}, {"feathers", "Feathers", "Black feathers floating down."}};
    for (const auto &a : amb)
        add(QStringLiteral("ambient.") + a.id, T(a.name), am, T(a.desc), "ambient", {{"metric", a.id}}, {1920, 1080});
    add("candles", T("Candle row"), am, T("A row of flickering candles."), "candles", {{"count", 7}}, {900, 220});
    add("webcam_frame", T("Gothic webcam frame"), am, T("A carved arch frame to put around your camera."), "webcam_frame", {}, {640, 480});
    add("divider", T("Ornament divider"), am, T("A filigree divider for layouts."), "divider", {}, {800, 80});
    return c;
}

QStringList widgetCategories() {
    return {T("Alerts & Events"), T("Labels"), T("Goals"), T("Chat"), T("Leaderboards"), T("Counters"), T("Time"), T("Stream Stats"),
            T("Info Panels"), T("Ambience & Decor")};
}

const WidgetCatalogEntry *widgetCatalogEntry(const QString &id) {
    for (const auto &e : widgetCatalog()) if (e.id == id) return &e;
    return nullptr;
}

QStringList widgetKinds() {
    QStringList kinds;
    for (const auto &e : widgetCatalog()) if (!kinds.contains(e.kind)) kinds << e.kind;
    return kinds;
}

QString widgetKindName(const QString &kind) {
    static const QHash<QString, const char *> names = {
        {"alerts", "Alert box"}, {"events", "Event list"}, {"ticker", "Ticker"}, {"recent_list", "Recent list"}, {"raid_banner", "Raid banner"},
        {"hype_meter", "Hype meter"}, {"combo", "Sub combo"}, {"label", "Label"}, {"goal", "Goal bar"}, {"goal_ring", "Ring goal"},
        {"goal_candle", "Candle goal"}, {"chat", "Chat box"}, {"chat_bubble", "Chat bubble"}, {"chat_marquee", "Chat marquee"},
        {"poll", "Chat poll"}, {"giveaway", "Giveaway"}, {"qna", "Q&A board"}, {"word_counter", "Word counter"}, {"first_chatter", "First chatter"},
        {"leaderboard", "Leaderboard"}, {"counter", "Counter"}, {"winloss", "Win / loss"}, {"clock", "Clock"}, {"analog_clock", "Gothic clock"},
        {"date", "Date"}, {"world_clock", "World clock"}, {"countdown", "Countdown"}, {"countdown_date", "Countdown to date"},
        {"stopwatch", "Stream uptime"}, {"subathon", "Subathon timer"}, {"pomodoro", "Pomodoro"}, {"moon_phase", "Moon phase"},
        {"viewers", "Viewer count"}, {"on_air", "ON AIR sign"}, {"rec_indicator", "Recording indicator"}, {"stream_health", "Stream health"},
        {"bitrate_graph", "Bitrate graph"}, {"fps_meter", "FPS meter"}, {"cpu_meter", "CPU meter"}, {"scene_name", "Current scene"},
        {"voice_sigil", "Voice sigil"}, {"audio_visualizer", "Audio visualizer"}, {"social", "Social panel"}, {"sponsor", "Sponsor panel"},
        {"text_panel", "Text panel"}, {"schedule", "List panel"}, {"rotator", "Rotating messages"}, {"lower_third", "Lower third"},
        {"banner", "Scrolling banner"}, {"file_text", "Text from file"}, {"slideshow", "Image slideshow"}, {"reminder", "Reminder"},
        {"ambient", "Ambience"}, {"candles", "Candle row"}, {"webcam_frame", "Webcam frame"}, {"divider", "Ornament divider"}};
    return T(names.value(kind, "Widget"));
}

} // namespace bc
