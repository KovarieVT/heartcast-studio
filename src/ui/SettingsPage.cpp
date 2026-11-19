#include "ui/SettingsPage.h"
#include "app/Theme.h"
#include "core/HardwareInfo.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "ui/StudioDocks.h"
#include "updater/Updater.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bc {

void applyProcessPriority() {
#ifdef _WIN32
    const QString p = Settings::instance().s("advanced.processPriority");
    SetPriorityClass(GetCurrentProcess(), p == "high" ? HIGH_PRIORITY_CLASS : p == "normal" ? NORMAL_PRIORITY_CLASS
                                         : p == "below_normal" ? BELOW_NORMAL_PRIORITY_CLASS : ABOVE_NORMAL_PRIORITY_CLASS);
#endif
}

void openMicTest(QWidget *parent) {
    auto *d = new QDialog(parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(QObject::tr("Microphone test ♡"));
    d->resize(520, 260);
    auto *lay = new QVBoxLayout(d);
    lay->addWidget(new QLabel(QObject::tr("Speak normally. Aim for the meter to peak in the pink zone (around −12 to −6 dB), never touching the red dot.")));
    obs_source_t *mic = Engine::instance().micAudio();
    if (!mic) { lay->addWidget(new QLabel(QObject::tr("No microphone source is available."))); d->show(); return; }
    auto *strip = new AudioStrip(mic, d);
    lay->addWidget(strip);
    auto *t = new QTimer(d);
    QObject::connect(t, &QTimer::timeout, strip, &AudioStrip::pollMeter);
    t->start(33);
    auto *hear = new QCheckBox(QObject::tr("Let me hear myself (use headphones!)"));
    const auto prev = obs_source_get_monitoring_type(mic);
    QObject::connect(hear, &QCheckBox::toggled, d, [mic](bool on) { obs_source_set_monitoring_type(mic, on ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT : OBS_MONITORING_TYPE_NONE); });
    QObject::connect(d, &QDialog::finished, d, [mic, prev] { obs_source_set_monitoring_type(mic, prev); });
    lay->addWidget(hear);
    auto *filters = new QPushButton(QObject::tr("Add noise suppression / compressor…"));
    QObject::connect(filters, &QPushButton::clicked, d, [mic, d] { (new FiltersDialog(mic, d))->show(); });
    lay->addWidget(filters);
    d->show();
}

// helpers to bind editors to settings keys
template <typename W> static W *reg(QHash<QString, QWidget *> &m, const QString &key, W *w) { m.insert(key, w); w->setObjectName(key); return w; }

static QComboBox *combo(const QList<QPair<QString, QVariant>> &items) {
    auto *c = new QComboBox;
    for (const auto &i : items) c->addItem(i.first, i.second);
    return c;
}

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Settings")); title->setObjectName("H1");
    lay->addWidget(title);
    m_tabs = new QTabWidget;
    lay->addWidget(m_tabs, 1);
    auto &e = Engine::instance();

    // ---- General
    auto *g = new QWidget; auto *gf = new QFormLayout(g);
    auto *lang = reg(m_fields, "general.language", combo({{"English", "en"}}));
    lang->setToolTip(tr("More languages can be added with Qt translation files (see docs)."));
    gf->addRow(tr("Language"), lang);
    gf->addRow(QString(), reg(m_fields, "general.startupAnimations", new QCheckBox(tr("Animated splash screen && decorations"))));
    gf->addRow(QString(), reg(m_fields, "general.reduceEffectsWhileLive", new QCheckBox(tr("Pause decorative animations while live or recording"))));
    gf->addRow(QString(), reg(m_fields, "general.notifications", new QCheckBox(tr("Show notifications (stream errors, alerts)"))));
    gf->addRow(QString(), reg(m_fields, "general.confirmStopStream", new QCheckBox(tr("Ask before ending a stream"))));
    gf->addRow(QString(), reg(m_fields, "general.checkUpdates", new QCheckBox(tr("Check for updates automatically"))));
    auto *autosave = reg(m_fields, "general.autosaveMinutes", new QSpinBox); autosave->setRange(1, 60); autosave->setSuffix(tr(" min"));
    gf->addRow(tr("Auto-save scenes every"), autosave);
    gf->addRow(tr("Crash reports"), reg(m_fields, "general.crashReports", combo({{tr("Save locally only"), "local"}, {tr("Off"), "off"}})));
    auto *wiz = new QPushButton(tr("Run the setup wizard again"));
    connect(wiz, &QPushButton::clicked, this, &SettingsPage::rerunWizard);
    auto *upd = new QPushButton(tr("Check for updates now"));
    connect(upd, &QPushButton::clicked, this, [] { Updater::instance().check(true); });
    gf->addRow(wiz); gf->addRow(upd);
    m_tabs->addTab(g, tr("General"));

    // ---- Appearance
    auto *a = new QWidget; auto *af = new QFormLayout(a);
    auto *accentBtn = new QPushButton;
    accentBtn->setObjectName("accentBtn");
    connect(accentBtn, &QPushButton::clicked, this, [this, accentBtn] {
        const QColor c = QColorDialog::getColor(QColor(Settings::instance().s("appearance.accent")), this, tr("Accent colour"));
        if (!c.isValid()) return;
        Settings::instance().set("appearance.accent", c.name());
        accentBtn->setText(c.name()); accentBtn->setStyleSheet("background:" + c.name());
        theme::apply(*qApp, c.name(), Settings::instance().i("appearance.fontPt"));
        Q_EMIT themeChanged();
    });
    m_fields.insert("appearance.accent", accentBtn);
    af->addRow(tr("Accent colour"), accentBtn);
    auto *presets = new QWidget; auto *ph = new QHBoxLayout(presets); ph->setContentsMargins(0, 0, 0, 0);
    for (const QString c : {"#F5A6C8", "#E98DB7", "#C96F9D", "#77CDB1", "#A9E5D0", "#DCCCF7"}) {
        auto *b = new QPushButton; b->setFixedSize(28, 28); b->setStyleSheet(QStringLiteral("background:%1;border-radius:14px;").arg(c));
        connect(b, &QPushButton::clicked, this, [this, c, accentBtn] {
            Settings::instance().set("appearance.accent", c);
            accentBtn->setText(c); accentBtn->setStyleSheet("background:" + c);
            theme::apply(*qApp, c, Settings::instance().i("appearance.fontPt"));
            Q_EMIT themeChanged();
        });
        ph->addWidget(b);
    }
    ph->addStretch();
    af->addRow(tr("Quick picks"), presets);
    auto *scale = reg(m_fields, "appearance.uiScale", new QDoubleSpinBox); scale->setRange(0.75, 2.5); scale->setSingleStep(0.05);
    af->addRow(tr("UI scale (restart)"), scale);
    auto *fpt = reg(m_fields, "appearance.fontPt", new QSpinBox); fpt->setRange(8, 16);
    af->addRow(tr("Font size"), fpt);
    af->addRow(QString(), reg(m_fields, "appearance.compact", new QCheckBox(tr("Compact mode (smaller paddings)"))));
    af->addRow(QString(), reg(m_fields, "appearance.sparkles", new QCheckBox(tr("Sparkle decorations"))));
    m_tabs->addTab(a, tr("Appearance"));

    // ---- Streaming / output
    auto *s = new QWidget; auto *sf = new QFormLayout(s);
    QList<QPair<QString, QVariant>> encs = {{tr("Automatic (best available)"), "auto"}};
    for (const auto &en : e.videoEncoders()) encs << qMakePair(QStringLiteral("%1  [%2%3]").arg(en.name, en.codec.toUpper(), en.hardware ? ", GPU" : ", CPU"), QVariant(en.id));
    sf->addRow(tr("Video encoder"), reg(m_fields, "output.encoder", combo(encs)));
    sf->addRow(tr("Rate control"), reg(m_fields, "output.rateControl", combo({{"CBR (recommended for streaming)", "CBR"}, {"VBR", "VBR"}, {"CQP / CRF", "CQP"}})));
    auto *br = reg(m_fields, "output.bitrate", new QSpinBox); br->setRange(500, 100000); br->setSuffix(" kbps"); br->setSingleStep(250);
    sf->addRow(tr("Video bitrate"), br);
    auto *ki = reg(m_fields, "output.keyintSec", new QSpinBox); ki->setRange(1, 10); ki->setSuffix(" s");
    sf->addRow(tr("Keyframe interval"), ki);
    sf->addRow(tr("Preset"), reg(m_fields, "output.preset", combo({{tr("Performance"), "performance"}, {tr("Balanced"), "balanced"}, {tr("Quality"), "quality"}})));
    sf->addRow(tr("Profile"), reg(m_fields, "output.profile", combo({{"high", "high"}, {"main", "main"}, {"baseline", "baseline"}})));
    auto *bf = reg(m_fields, "output.bframes", new QSpinBox); bf->setRange(0, 4);
    sf->addRow(tr("B-frames"), bf);
    sf->addRow(tr("Audio codec"), reg(m_fields, "output.audioCodec", combo({{"AAC (all platforms)", "aac"}, {"Opus (SRT/WHIP/recordings)", "opus"}})));
    sf->addRow(tr("Audio bitrate"), reg(m_fields, "output.audioBitrate", combo({{"96 kbps", 96}, {"128 kbps", 128}, {"160 kbps", 160}, {"192 kbps", 192}, {"256 kbps", 256}, {"320 kbps", 320}})));
    auto *rr = reg(m_fields, "output.reconnectRetries", new QSpinBox); rr->setRange(0, 100);
    sf->addRow(tr("Reconnect attempts"), rr);
    auto *rd = reg(m_fields, "output.reconnectDelaySec", new QSpinBox); rd->setRange(1, 60); rd->setSuffix(" s");
    sf->addRow(tr("Reconnect delay"), rd);
    const HardwareInfo hw = HardwareInfo::detect();
    QStringList gpus; for (const auto &gp : hw.gpus) gpus << gp.name;
    auto *hwl = new QLabel(tr("Detected: %1 · %2 threads · %3 GB RAM · %4<br>Recommended profile: <b>%5</b>")
        .arg(hw.cpuName).arg(hw.logicalCores).arg(hw.ramMB / 1024).arg(gpus.isEmpty() ? tr("no dedicated GPU found") : gpus.join(", "), hw.recommendedProfile()));
    hwl->setWordWrap(true);
    sf->addRow(hwl);
    auto *profRow = new QWidget; auto *prh = new QHBoxLayout(profRow); prh->setContentsMargins(0, 0, 0, 0);
    for (const auto &[id, label] : QList<QPair<QString, QString>>{{"low", tr("Low-end PC")}, {"balanced", tr("Balanced")}, {"high", tr("High performance")}, {"pro", tr("Pro streaming PC")}}) {
        auto *b = new QPushButton(label);
        connect(b, &QPushButton::clicked, this, [this, id = id] {
            applyPerformanceProfile(id, Engine::instance().bestEncoder());
            load();
            QMessageBox::information(this, tr("Profile"), tr("Profile applied — press Apply to use it."));
        });
        prh->addWidget(b);
    }
    sf->addRow(tr("Performance profile"), profRow);
    m_tabs->addTab(s, tr("Streaming"));

    // ---- Audio
    auto *au = new QWidget; auto *auf = new QFormLayout(au);
    auf->addRow(tr("Sample rate (restart)"), reg(m_fields, "audio.sampleRate", combo({{"44.1 kHz", 44100}, {"48 kHz", 48000}})));
    auf->addRow(tr("Channels (restart)"), reg(m_fields, "audio.channels", combo({{tr("Stereo"), "stereo"}, {tr("Mono"), "mono"}})));
    QList<QPair<QString, QVariant>> outs, ins, mons;
    for (const auto &[id, name] : e.audioDevices(false)) outs << qMakePair(name, QVariant(id));
    for (const auto &[id, name] : e.audioDevices(true)) ins << qMakePair(name, QVariant(id));
    if (outs.isEmpty()) outs << qMakePair(tr("Default"), QVariant("default"));
    if (ins.isEmpty()) ins << qMakePair(tr("Default"), QVariant("default"));
    obs_enum_audio_monitoring_devices([](void *p, const char *name, const char *id) {
        static_cast<QList<QPair<QString, QVariant>> *>(p)->append(qMakePair(QString::fromUtf8(name), QVariant(QString::fromUtf8(id))));
        return true;
    }, &mons);
    if (mons.isEmpty()) mons << qMakePair(tr("Default"), QVariant("default"));
    auf->addRow(tr("Desktop audio"), reg(m_fields, "audio.desktopDevice", combo(outs)));
    auf->addRow(tr("Microphone"), reg(m_fields, "audio.micDevice", combo(ins)));
    auf->addRow(tr("Monitoring device"), reg(m_fields, "audio.monitorDevice", combo(mons)));
    auto *micTest = new QPushButton(tr("Test microphone ♡"));
    connect(micTest, &QPushButton::clicked, this, [this] { openMicTest(this); });
    auf->addRow(micTest);
    auto *ah = new QLabel(tr("Per-source volume, mute, monitoring, sync offset, balance, tracks and filters live in the Audio Mixer."));
    ah->setWordWrap(true); ah->setObjectName("Muted");
    auf->addRow(ah);
    m_tabs->addTab(au, tr("Audio"));

    // ---- Video
    auto *v = new QWidget; auto *vf = new QFormLayout(v);
    const QList<QPair<QString, QVariant>> res = {{"1920×1080 (16:9)", "1920x1080"}, {"2560×1440", "2560x1440"}, {"3840×2160 (4K)", "3840x2160"},
        {"1280×720", "1280x720"}, {"1080×1920 (vertical)", "1080x1920"}, {"720×1280 (vertical)", "720x1280"}, {"1080×1080 (square)", "1080x1080"}};
    auto *base = reg(m_fields, "video.base", combo(res)); base->setEditable(true);
    auto *out = reg(m_fields, "video.out", combo(res)); out->setEditable(true);
    vf->addRow(tr("Canvas (base) resolution"), base);
    vf->addRow(tr("Output (scaled) resolution"), out);
    vf->addRow(tr("Frame rate"), reg(m_fields, "video.fps", combo({{"24", "24/1"}, {"25", "25/1"}, {"29.97", "30000/1001"}, {"30", "30/1"},
                                                                   {"48", "48/1"}, {"50", "50/1"}, {"59.94", "60000/1001"}, {"60", "60/1"}, {"120", "120/1"}})));
    vf->addRow(tr("Downscale filter"), reg(m_fields, "video.scaleType", combo({{"Bicubic", "bicubic"}, {"Lanczos (sharper)", "lanczos"}, {"Area", "area"}, {"Bilinear (fastest)", "bilinear"}})));
    vf->addRow(tr("Colour space"), reg(m_fields, "video.colorSpace", combo({{"Rec. 709", "709"}, {"Rec. 601", "601"}})));
    vf->addRow(tr("Colour range"), reg(m_fields, "video.colorRange", combo({{tr("Limited (recommended)"), "partial"}, {tr("Full"), "full"}})));
    m_tabs->addTab(v, tr("Video"));

    // ---- Recording
    auto *r = new QWidget; auto *rf = new QFormLayout(r);
    auto *path = reg(m_fields, "recording.path", new QLineEdit);
    auto *browse = new QPushButton(tr("Browse…"));
    connect(browse, &QPushButton::clicked, this, [this, path] { const QString d = QFileDialog::getExistingDirectory(this, tr("Recording folder"), path->text()); if (!d.isEmpty()) path->setText(d); });
    auto *pr = new QWidget; auto *prl = new QHBoxLayout(pr); prl->setContentsMargins(0, 0, 0, 0); prl->addWidget(path, 1); prl->addWidget(browse);
    rf->addRow(tr("Folder"), pr);
    rf->addRow(tr("Format"), reg(m_fields, "recording.format", combo({{tr("MKV (safe, recommended)"), "mkv"}, {"MP4", "mp4"}, {"MOV", "mov"}, {"FLV", "flv"}, {"MPEG-TS", "ts"}})));
    rf->addRow(tr("Quality"), reg(m_fields, "recording.quality", combo({{tr("Same as stream (no extra encode)"), "stream"}, {tr("High quality"), "high"},
                                                                        {tr("Indistinguishable"), "indistinguishable"}, {tr("Lossless (huge files)"), "lossless"}})));
    auto *tr_ = reg(m_fields, "recording.tracks", new QSpinBox); tr_->setRange(1, 6);
    rf->addRow(tr("Audio tracks"), tr_);
    rf->addRow(QString(), reg(m_fields, "recording.autoRecordWhenLive", new QCheckBox(tr("Record automatically while streaming"))));
    rf->addRow(QString(), reg(m_fields, "recording.remuxToMp4", new QCheckBox(tr("Make an MP4 copy after recording (MKV only)"))));
    auto *rb = reg(m_fields, "recording.replayBufferSec", new QSpinBox); rb->setRange(5, 21600); rb->setSuffix(" s");
    rf->addRow(tr("Replay buffer length"), rb);
    m_tabs->addTab(r, tr("Recording"));

    // ---- Hotkeys
    auto *hk = new QWidget; auto *hl = new QVBoxLayout(hk);
    auto *table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut"), tr("Status")});
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    hl->addWidget(new QLabel(tr("Shortcuts are global on Windows — they work while your game has focus.")));
    hl->addWidget(table, 1);
    auto fillHotkeys = [table] {
        const auto acts = HotkeyManager::instance().actions();
        table->setRowCount(int(acts.size()));
        for (int i = 0; i < acts.size(); ++i) {
            table->setItem(i, 0, new QTableWidgetItem(acts[i].group + " · " + acts[i].label));
            auto *ed = new QKeySequenceEdit(HotkeyManager::instance().binding(acts[i].id));
            auto *status = new QTableWidgetItem(HotkeyManager::instance().failedRegistrations().contains(acts[i].id) ? QObject::tr("in use by another app") : QString());
            table->setCellWidget(i, 1, ed);
            table->setItem(i, 2, status);
            const QString id = acts[i].id;
            QObject::connect(ed, &QKeySequenceEdit::editingFinished, ed, [ed, id, status] {
                QKeySequence seq = ed->keySequence();
                if (seq.count() > 1) seq = QKeySequence(seq[0]);
                const QString err = HotkeyManager::instance().setBinding(id, seq);
                status->setText(err.isEmpty() ? (seq.isEmpty() ? QString() : QObject::tr("✓ saved")) : err);
                status->setForeground(err.isEmpty() ? QColor("#3E9C7E") : QColor("#C9467F"));
            });
        }
    };
    auto *refreshHk = new QPushButton(tr("Refresh list"));
    connect(refreshHk, &QPushButton::clicked, this, fillHotkeys);
    hl->addWidget(refreshHk);
    QTimer::singleShot(0, this, fillHotkeys);
    m_tabs->addTab(hk, tr("Hotkeys"));

    // ---- Advanced
    auto *ad = new QWidget; auto *adf = new QFormLayout(ad);
    adf->addRow(tr("Process priority"), reg(m_fields, "advanced.processPriority", combo({{tr("High"), "high"}, {tr("Above normal (recommended)"), "above_normal"}, {tr("Normal"), "normal"}, {tr("Below normal"), "below_normal"}})));
    auto *gpu = reg(m_fields, "advanced.gpuIndex", new QSpinBox); gpu->setRange(0, 8);
    adf->addRow(tr("Render GPU index (restart)"), gpu);
    adf->addRow(tr("Update feed (HTTPS)"), reg(m_fields, "updates.feed", new QLineEdit));
    auto *logs = new QPushButton(tr("Open logs folder"));
    connect(logs, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logs())); });
    auto *crashes = new QPushButton(tr("Open crash reports"));
    connect(crashes, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::crashes())); });
    auto *cache = new QPushButton(tr("Clear cache (thumbnails, generated images)"));
    connect(cache, &QPushButton::clicked, this, [this] { QDir(Paths::cache() + "/thumbs").removeRecursively(); QMessageBox::information(this, tr("Cache"), tr("Cache cleared ✦")); });
    auto *exp = new QPushButton(tr("Export configuration…"));
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Export configuration"), "heartcast-config.json", "*.json");
        if (!f.isEmpty() && Settings::instance().exportTo(f))
            QMessageBox::information(this, tr("Export"), tr("Exported. Stream keys and account tokens are NOT included (they stay protected on this PC)."));
    });
    auto *imp = new QPushButton(tr("Import configuration…"));
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import configuration"), QString(), "*.json");
        QString err;
        if (!f.isEmpty() && !Settings::instance().importFrom(f, &err)) QMessageBox::warning(this, tr("Import"), err);
        else if (!f.isEmpty()) { load(); QMessageBox::information(this, tr("Import"), tr("Imported — some changes need a restart.")); }
    });
    auto *reset = new QPushButton(tr("Reset all settings to defaults"));
    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Reset"), tr("Reset every setting? Scenes, recordings and saved keys are kept.")) == QMessageBox::Yes) { Settings::instance().resetToDefaults(); load(); }
    });
    for (auto *b : {logs, crashes, cache, exp, imp, reset}) adf->addRow(b);
    auto *dev = new QLabel(tr("Platform developer credentials (Client IDs) are read from environment variables or %1 — see API-INTEGRATIONS.md. They are never compiled into the app.")
                           .arg(QDir::toNativeSeparators(Paths::dataRoot() + "/developer.json")));
    dev->setWordWrap(true); dev->setObjectName("Muted");
    adf->addRow(dev);
    m_tabs->addTab(ad, tr("Advanced"));

    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *revert = new QPushButton(tr("Revert"));
    auto *applyBtn = new QPushButton(tr("Apply ♡")); applyBtn->setProperty("accent", "pink");
    row->addWidget(revert); row->addWidget(applyBtn);
    lay->addLayout(row);
    connect(revert, &QPushButton::clicked, this, &SettingsPage::load);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsPage::apply);
    load();
}

void SettingsPage::load() {
    auto &s = Settings::instance();
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        const QString k = it.key();
        QWidget *w = it.value();
        QVariant v = s.get(k);
        if (k == "video.base") v = QStringLiteral("%1x%2").arg(s.i("video.baseWidth")).arg(s.i("video.baseHeight"));
        if (k == "video.out") v = QStringLiteral("%1x%2").arg(s.i("video.outWidth")).arg(s.i("video.outHeight"));
        if (k == "video.fps") v = QStringLiteral("%1/%2").arg(s.i("video.fpsNum")).arg(s.i("video.fpsDen"));
        if (auto *c = qobject_cast<QCheckBox *>(w)) c->setChecked(v.toBool());
        else if (auto *sp = qobject_cast<QSpinBox *>(w)) sp->setValue(v.toInt());
        else if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w)) dsp->setValue(v.toDouble());
        else if (auto *le = qobject_cast<QLineEdit *>(w)) le->setText(v.toString());
        else if (auto *cb = qobject_cast<QComboBox *>(w)) {
            int idx = cb->findData(v);
            if (idx < 0) idx = cb->findData(v.toString());
            if (idx < 0) idx = cb->findData(v.toInt());
            if (idx >= 0) cb->setCurrentIndex(idx);
            else if (cb->isEditable()) cb->setEditText(v.toString());
        } else if (auto *b = qobject_cast<QPushButton *>(w)) { b->setText(v.toString()); b->setStyleSheet("background:" + v.toString()); }
    }
}

void SettingsPage::apply() {
    auto &s = Settings::instance();
    auto &e = Engine::instance();
    const auto before = s.raw();
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        const QString k = it.key();
        QWidget *w = it.value();
        QVariant v;
        if (auto *c = qobject_cast<QCheckBox *>(w)) v = c->isChecked();
        else if (auto *sp = qobject_cast<QSpinBox *>(w)) v = sp->value();
        else if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w)) v = dsp->value();
        else if (auto *le = qobject_cast<QLineEdit *>(w)) v = le->text();
        else if (auto *cb = qobject_cast<QComboBox *>(w)) v = cb->currentData().isValid() && cb->currentText() == cb->itemText(cb->currentIndex()) ? cb->currentData() : QVariant(cb->currentText());
        else continue;
        if (k == "video.base" || k == "video.out") {
            const QString t = v.toString().replace("×", "x");
            const int wv = t.section('x', 0, 0).toInt(), hv = t.section('x', 1, 1).toInt();
            if (wv < 64 || hv < 64 || wv > 8192 || hv > 8192) { QMessageBox::warning(this, tr("Video"), tr("Invalid resolution: %1").arg(t)); return; }
            const QString pre = k == "video.base" ? "video.base" : "video.out";
            s.set(pre + "Width", wv & ~1); s.set(pre + "Height", hv & ~1);   // encoders need even sizes
            continue;
        }
        if (k == "video.fps") { s.set("video.fpsNum", v.toString().section('/', 0, 0).toInt()); s.set("video.fpsDen", qMax(1, v.toString().section('/', 1, 1).toInt())); continue; }
        if (k == "updates.feed" && !v.toString().isEmpty() && !v.toString().startsWith("https://")) { QMessageBox::warning(this, tr("Updates"), tr("The update feed must use HTTPS.")); return; }
        s.set(k, v);
    }
    const auto after = s.raw();
    QStringList notes;
    if (before.value("video") != after.value("video")) {
        if (e.isStreaming() || e.isRecording() || e.isReplayBufferActive() || e.isVirtualCamActive()) {
            notes << tr("Video changes will apply after you stop all outputs and press Apply again.");
            s.set("video", before.value("video").toVariant());
        } else {
            QString err;
            if (!e.resetVideo(&err)) notes << err;
        }
    }
    if (before.value("audio").toObject().value("sampleRate") != after.value("audio").toObject().value("sampleRate")
        || before.value("audio").toObject().value("channels") != after.value("audio").toObject().value("channels"))
        notes << tr("Audio sample rate/channel changes take effect after restarting HeartCast.");
    if (s.s("audio.micDevice") != before.value("audio").toObject().value("micDevice").toString()) e.setAudioDevice(true, s.s("audio.micDevice"));
    if (s.s("audio.desktopDevice") != before.value("audio").toObject().value("desktopDevice").toString()) e.setAudioDevice(false, s.s("audio.desktopDevice"));
    if (auto *mon = qobject_cast<QComboBox *>(m_fields.value("audio.monitorDevice"))) e.setMonitoringDevice(mon->currentText(), mon->currentData().toString());
    applyProcessPriority();
    theme::apply(*qApp, s.s("appearance.accent"), s.i("appearance.fontPt"));
    s.save();
    Q_EMIT themeChanged();
    QMessageBox::information(this, tr("Settings"), notes.isEmpty() ? tr("Saved ♡") : notes.join("\n"));
}

} // namespace bc
