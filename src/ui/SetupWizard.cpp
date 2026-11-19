#include "ui/SetupWizard.h"
#include "ui/DeveloperCredentialsDialog.h"
#include "app/Theme.h"
#include "core/HardwareInfo.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QButtonGroup>
#include <QCamera>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QBuffer>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QtMath>

namespace bc {

class FieldPage : public QWizardPage {
public:
    using QWizardPage::registerField;
};

static FieldPage *makePage(const QString &title, const QString &sub) {
    auto *p = new FieldPage;
    p->setTitle(title);
    p->setSubTitle(sub);
    return p;
}

// --- tiny helpers for live device checks (stopped when the page is left)
class MicLevelPage : public QWizardPage {
public:
    MicLevelPage() {
        setTitle(tr("Microphone ♪"));
        setSubTitle(tr("Pick your microphone and say hi — the bar should bounce."));
        auto *f = new QFormLayout(this);
        combo = new QComboBox;
        for (const auto &d : QMediaDevices::audioInputs()) combo->addItem(d.description(), d.id());
        if (combo->count() == 0) combo->addItem(tr("No microphone found"), QByteArray());
        level = new QProgressBar; level->setRange(0, 100); level->setTextVisible(false);
        f->addRow(tr("Microphone"), combo);
        f->addRow(tr("Level"), level);
        registerField("micName", combo, "currentText");
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { start(); });
    }
    void initializePage() override { start(); }
    void cleanupPage() override { stop(); }
    bool validatePage() override { stop(); return true; }
    QComboBox *combo; QProgressBar *level;
private:
    void stop() { if (src) { src->stop(); delete src; src = nullptr; } }
    void start() {
        stop();
        QAudioDevice dev;
        for (const auto &d : QMediaDevices::audioInputs()) if (d.id() == combo->currentData().toByteArray()) dev = d;
        if (dev.isNull()) return;
        QAudioFormat fmt; fmt.setSampleRate(48000); fmt.setChannelCount(1); fmt.setSampleFormat(QAudioFormat::Int16);
        if (!dev.isFormatSupported(fmt)) fmt = dev.preferredFormat();
        src = new QAudioSource(dev, fmt, this);
        QIODevice *io = src->start();
        if (!io) return;
        connect(io, &QIODevice::readyRead, this, [this, io, fmt] {
            const QByteArray b = io->readAll();
            double peak = 0;
            if (fmt.sampleFormat() == QAudioFormat::Int16) {
                const auto *s = reinterpret_cast<const qint16 *>(b.constData());
                for (int i = 0; i < b.size() / 2; ++i) peak = qMax(peak, std::abs(s[i]) / 32768.0);
            } else if (fmt.sampleFormat() == QAudioFormat::Float) {
                const auto *s = reinterpret_cast<const float *>(b.constData());
                for (int i = 0; i < b.size() / 4; ++i) peak = qMax(peak, double(std::abs(s[i])));
            }
            const double db = 20 * std::log10(qMax(peak, 1e-6));
            level->setValue(int(qBound(0.0, (db + 60) / 60 * 100, 100.0)));
        });
    }
    QAudioSource *src = nullptr;
};

class CameraPage : public QWizardPage {
public:
    CameraPage() {
        setTitle(tr("Camera ✦"));
        setSubTitle(tr("Choose a webcam or capture card. You can add more later as sources."));
        auto *v = new QVBoxLayout(this);
        combo = new QComboBox;
        combo->addItem(tr("No camera"), QByteArray());
        for (const auto &c : QMediaDevices::videoInputs()) combo->addItem(c.description(), c.id());
        video = new QVideoWidget; video->setMinimumHeight(220);
        v->addWidget(combo); v->addWidget(video, 1);
        registerField("cameraName", combo, "currentText");
        session.setVideoOutput(video);
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { start(); });
    }
    void initializePage() override { start(); }
    void cleanupPage() override { stop(); }
    bool validatePage() override { stop(); return true; }
private:
    void stop() { if (cam) { cam->stop(); delete cam; cam = nullptr; } }
    void start() {
        stop();
        for (const auto &c : QMediaDevices::videoInputs())
            if (c.id() == combo->currentData().toByteArray()) { cam = new QCamera(c, this); session.setCamera(cam); cam->start(); }
    }
    QComboBox *combo; QVideoWidget *video; QMediaCaptureSession session; QCamera *cam = nullptr;
};

SetupWizard::SetupWizard(QWidget *parent) : QWizard(parent) {
    setWindowTitle(tr("Welcome to HeartCast Studio ♡"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::HaveCustomButton1, true);
    setButtonText(QWizard::CustomButton1, tr("Skip setup"));
    connect(this, &QWizard::customButtonClicked, this, [this] { Settings::instance().set("general.firstRunComplete", true); Settings::instance().save(); reject(); });
    setPixmap(QWizard::LogoPixmap, QPixmap(":/icons/logo_256.png").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    resize(820, 600);
    const HardwareInfo hw = HardwareInfo::detect();

    // Welcome
    auto *w = makePage(tr("Hi there, cutie! ♡"), tr("Let's set up your streaming studio. It takes about a minute."));
    auto *wl = new QVBoxLayout(w);
    auto *logo = new QLabel; logo->setPixmap(QPixmap(":/icons/logo_256.png").scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    wl->addWidget(logo, 0, Qt::AlignHCenter);
    auto *beg = new QRadioButton(tr("Beginner setup — pick recommended settings for me (best for most people)"));
    auto *adv = new QRadioButton(tr("Advanced setup — let me choose every setting"));
    beg->setChecked(Settings::instance().s("general.setupMode") != "advanced");
    wl->addWidget(beg); wl->addWidget(adv);
    w->registerField("beginner", beg);
    setPage(Welcome, w);

    // Hardware
    auto *h = makePage(tr("Your hardware"), tr("We checked your PC so your stream stays smooth while you game."));
    auto *hf = new QFormLayout(h);
    QStringList gpus; for (const auto &g : hw.gpus) gpus << QStringLiteral("%1 (%2 GB)").arg(g.name).arg(g.vramMB / 1024);
    hf->addRow(tr("CPU"), new QLabel(QStringLiteral("%1 — %2 threads").arg(hw.cpuName).arg(hw.logicalCores)));
    hf->addRow(tr("Memory"), new QLabel(QStringLiteral("%1 GB").arg(hw.ramMB / 1024.0, 0, 'f', 1)));
    hf->addRow(tr("Graphics"), new QLabel(gpus.isEmpty() ? tr("No dedicated GPU detected") : gpus.join("\n")));
    auto *prof = new QComboBox;
    prof->addItem(tr("Low-end PC (720p30)"), "low"); prof->addItem(tr("Balanced (1080p30)"), "balanced");
    prof->addItem(tr("High-performance (1080p60)"), "high"); prof->addItem(tr("Professional (1440p60)"), "pro");
    prof->setCurrentIndex(prof->findData(hw.recommendedProfile()));
    hf->addRow(tr("Recommended profile"), prof);
    h->registerField("profile", prof, "currentData");
    setPage(Hardware, h);

    setPage(Camera, new CameraPage);
    setPage(Microphone, new MicLevelPage);

    // Output device
    auto *o = makePage(tr("Speakers / headphones"), tr("Where should HeartCast play monitoring and alert previews?"));
    auto *of = new QFormLayout(o);
    auto *outCombo = new QComboBox;
    for (const auto &d : QMediaDevices::audioOutputs()) outCombo->addItem(d.description(), d.id());
    if (outCombo->count() == 0) outCombo->addItem(tr("Default"), QByteArray());
    auto *beep = new QPushButton(tr("Play test sound ♪"));
    connect(beep, &QPushButton::clicked, o, [outCombo, o] {
        QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        for (const auto &d : QMediaDevices::audioOutputs()) if (d.id() == outCombo->currentData().toByteArray()) dev = d;
        QAudioFormat fmt; fmt.setSampleRate(48000); fmt.setChannelCount(1); fmt.setSampleFormat(QAudioFormat::Int16);
        auto *sink = new QAudioSink(dev, fmt, o);
        auto *buf = new QBuffer(sink);
        QByteArray pcm(48000 * 2 * 6 / 10, 0);   // 0.6 s two-note chime
        auto *s = reinterpret_cast<qint16 *>(pcm.data());
        for (int i = 0; i < pcm.size() / 2; ++i) {
            const double t = i / 48000.0, f = t < 0.3 ? 880.0 : 1318.5, env = std::exp(-6 * std::fmod(t, 0.3));
            s[i] = qint16(9000 * env * std::sin(2 * M_PI * f * t));
        }
        buf->setData(pcm); buf->open(QIODevice::ReadOnly);
        sink->start(buf);
        QTimer::singleShot(1200, sink, &QObject::deleteLater);
    });
    of->addRow(tr("Output"), outCombo);
    of->addRow(beep);
    o->registerField("outputName", outCombo, "currentText");
    setPage(Output, o);

    // Resolution
    auto *r = makePage(tr("Canvas & resolution"), tr("Landscape for Twitch/YouTube, vertical for TikTok / Shorts."));
    auto *rf = new QFormLayout(r);
    auto *res = new QComboBox;
    for (const auto &[label, val] : QList<QPair<QString, QString>>{{"1920×1080 (landscape)", "1920x1080"}, {"1280×720", "1280x720"},
             {"2560×1440", "2560x1440"}, {"3840×2160 (4K)", "3840x2160"}, {"1080×1920 (vertical)", "1080x1920"},
             {"720×1280 (vertical)", "720x1280"}, {"1080×1080 (square)", "1080x1080"}}) res->addItem(label, val);
    rf->addRow(tr("Canvas"), res);
    r->registerField("resolution", res, "currentData");
    setPage(Resolution, r);

    auto *fr = makePage(tr("Frame rate"), tr("60 fps is silky for games; 30 fps saves bandwidth and CPU."));
    auto *ff = new QFormLayout(fr);
    auto *fps = new QComboBox;
    for (int f : {30, 60, 24, 50}) fps->addItem(QStringLiteral("%1 fps").arg(f), f);
    ff->addRow(tr("FPS"), fps);
    fr->registerField("fps", fps, "currentData");
    setPage(FrameRate, fr);

    auto *en = makePage(tr("Encoder"), tr("GPU encoders keep your games smooth. We picked the best one we found."));
    auto *ef = new QFormLayout(en);
    auto *enc = new QComboBox;
    const QString best = Engine::instance().bestEncoder();
    for (const auto &e : Engine::instance().videoEncoders())
        enc->addItem(QStringLiteral("%1%2").arg(e.name, e.id == best ? tr("  ✦ recommended") : QString()), e.id);
    enc->setCurrentIndex(qMax(0, enc->findData(best)));
    auto *bitrate = new QSpinBox; bitrate->setRange(1000, 50000); bitrate->setSingleStep(500); bitrate->setValue(6000); bitrate->setSuffix(" kbps");
    ef->addRow(tr("Video encoder"), enc);
    ef->addRow(tr("Bitrate"), bitrate);
    en->registerField("encoder", enc, "currentData");
    en->registerField("bitrate", bitrate);
    setPage(Encoder, en);

    // Platform
    auto *pl = makePage(tr("Where do you stream?"), tr("Add your first destination. You can add more (multistream) in Stream Manager."));
    auto *pf = new QFormLayout(pl);
    auto *plat = new QComboBox;
    plat->addItem(tr("Skip for now"), QString());
    for (const auto &p : PlatformCatalog::all()) plat->addItem(p.name, p.id);
    auto *server = new QLineEdit; auto *key = new QLineEdit; key->setEchoMode(QLineEdit::Password);
    auto *connectBtn = new QPushButton(tr("Connect account in browser"));
    connectBtn->setProperty("accent", "mint");
    auto *help = new QLabel; help->setWordWrap(true); help->setObjectName("Muted"); help->setOpenExternalLinks(true);
    pf->addRow(tr("Platform"), plat);
    pf->addRow(tr("Server URL"), server);
    pf->addRow(tr("Stream key"), key);
    pf->addRow(connectBtn);
    pf->addRow(help);
    auto update = [=] {
        const PlatformInfo p = PlatformCatalog::get(plat->currentData().toString());
        const bool none = plat->currentData().toString().isEmpty();
        server->setEnabled(!none); key->setEnabled(!none);
        connectBtn->setVisible(!none && PlatformHub::instance().account(p.id));
        server->setText(none ? QString() : p.defaultServer);
        help->setText(none ? QString() : p.keyHelp + (p.limitations.isEmpty() ? QString() : "\n\n" + p.limitations));
    };
    connect(plat, &QComboBox::currentIndexChanged, pl, update);
    connect(connectBtn, &QPushButton::clicked, pl, [plat, help, pl] {
        const QString id = plat->currentData().toString();
        PlatformAccount *a = PlatformHub::instance().account(id);
        if (!a) return;
        if (!ensureDevCredentials(id, pl)) {
            help->setText(tr("Connecting needs a developer Client ID first. You can skip that for now: paste your stream key above and connect later from Stream Manager."));
            return;
        }
        a->connectAccount();
        help->setText(tr("Finish signing in in your browser. Your password is never entered in HeartCast."));
    });
    connect(&PlatformHub::instance(), &PlatformHub::deviceCode, pl, [help](const QString &, const QString &code, const QString &url) {
        help->setText(tr("Open <a href='%1'>%1</a> and enter code <b style='font-size:15pt'>%2</b>").arg(url, code));
    });
    connect(&PlatformHub::instance(), &PlatformHub::accountsChanged, pl, [help] { help->setText(tr("Account connected ♡ — the stream key will be fetched when you go live.")); });
    update();
    pl->registerField("platform", plat, "currentData");
    pl->registerField("server", server);
    pl->registerField("key", key);
    setPage(Platform, pl);

    // Theme
    auto *th = makePage(tr("Make it yours ♡"), tr("Pick an accent colour and how sparkly you want things."));
    auto *tl = new QVBoxLayout(th);
    auto *grp = new QButtonGroup(th);
    auto *row = new QHBoxLayout;
    int idx = 0;
    for (const auto &[name, col] : QList<QPair<QString, QString>>{{tr("Cotton Candy"), "#F5A6C8"}, {tr("Strawberry"), "#E98DB7"},
             {tr("Deep Rose"), "#C96F9D"}, {tr("Mint"), "#77CDB1"}, {tr("Lavender"), "#B9A3EA"}}) {
        auto *b = new QRadioButton(name);
        b->setStyleSheet(QStringLiteral("QRadioButton::indicator:checked{background:%1;}").arg(col));
        b->setProperty("color", col);
        grp->addButton(b, idx++);
        row->addWidget(b);
        connect(b, &QRadioButton::toggled, th, [col](bool on) { if (on) theme::apply(*qApp, col, Settings::instance().i("appearance.fontPt")); });
        if (col == Settings::instance().s("appearance.accent")) b->setChecked(true);
    }
    if (!grp->checkedButton()) grp->button(0)->setChecked(true);
    tl->addLayout(row);
    auto *anim = new QCheckBox(tr("Animated splash screen and decorations"));
    anim->setChecked(true);
    tl->addWidget(anim);
    tl->addStretch();
    th->registerField("animations", anim);
    connect(grp, &QButtonGroup::idClicked, th, [grp] { Settings::instance().set("appearance.accent", grp->checkedButton()->property("color").toString()); });
    setPage(Theme, th);

    auto *fin = makePage(tr("All set! ✦"), tr("Your studio is ready. Here's what we'll configure:"));
    auto *finL = new QVBoxLayout(fin);
    auto *summary = new QLabel; summary->setWordWrap(true);
    finL->addWidget(summary);
    connect(this, &QWizard::currentIdChanged, this, [this, summary](int id) {
        if (id != Finish) return;
        const QString p = field("profile").toString();
        summary->setText(tr("• Mode: %1\n• Performance profile: %2\n• Camera: %3\n• Microphone: %4\n• Destination: %5\n\nYou can change all of this later in Settings.")
            .arg(beginner() ? tr("Beginner (auto)") : tr("Advanced"), p, field("cameraName").toString(), field("micName").toString(),
                 field("platform").toString().isEmpty() ? tr("none yet") : PlatformCatalog::get(field("platform").toString()).name));
    });
    setPage(Finish, fin);
    setStartId(Welcome);
}

bool SetupWizard::beginner() const { return field("beginner").toBool(); }

int SetupWizard::nextId() const {
    const int id = currentId();
    if (beginner()) {
        // Beginner: devices + platform + theme; resolution/fps/encoder come from the hardware profile.
        switch (id) {
        case Output: return Platform;
        default: break;
        }
    }
    return id == Finish ? -1 : id + 1;
}

void SetupWizard::accept() {
    auto &s = Settings::instance();
    auto &e = Engine::instance();
    s.set("general.setupMode", beginner() ? "beginner" : "advanced");
    applyPerformanceProfile(field("profile").toString(), e.bestEncoder());
    if (!beginner()) {
        const QString res = field("resolution").toString();
        const int w = res.section('x', 0, 0).toInt(), h = res.section('x', 1, 1).toInt();
        if (w > 0 && h > 0) { s.set("video.baseWidth", w); s.set("video.baseHeight", h); s.set("video.outWidth", w); s.set("video.outHeight", h); }
        s.set("video.fpsNum", field("fps").toInt()); s.set("video.fpsDen", 1);
        s.set("output.encoder", field("encoder").toString());
        s.set("output.bitrate", field("bitrate").toInt());
    }
    const QString plat = field("platform").toString();
    if (!plat.isEmpty() && PlatformCatalog::get(plat).recommended.contains("canvas") && beginner()) {
        const QString c = PlatformCatalog::get(plat).recommended.value("canvas").toString();   // vertical platforms
        s.set("video.baseWidth", c.section('x', 0, 0).toInt()); s.set("video.baseHeight", c.section('x', 1, 1).toInt());
        s.set("video.outWidth", c.section('x', 0, 0).toInt()); s.set("video.outHeight", c.section('x', 1, 1).toInt());
    }
    s.set("general.startupAnimations", field("animations").toBool());
    s.set("general.firstRunComplete", true);
    s.save();

    QString err;
    if (!e.isStreaming() && !e.isRecording()) e.resetVideo(&err);

    // Mic + camera choices: point the global mic at the chosen device name, add camera to scenes.
    const QString mic = field("micName").toString();
    for (const auto &[id, name] : e.audioDevices(true)) if (name == mic) e.setAudioDevice(true, id);
    const QString cam = field("cameraName").toString();
    if (!cam.isEmpty() && cam != tr("No camera")) {
#ifdef _WIN32
        const char *camType = "dshow_input";
#else
        const char *camType = "v4l2_input";
#endif
        OBSDataAutoRelease st = obs_data_create();
        OBSSourceAutoRelease tmp = obs_source_create_private(camType, "probe", nullptr);
        if (tmp) {
            obs_properties_t *props = obs_source_properties(tmp);
            obs_property_t *p = obs_properties_get(props, "video_device_id");
            if (!p) p = obs_properties_get(props, "device_id");
            for (size_t i = 0; p && i < obs_property_list_item_count(p); ++i)
                if (QString::fromUtf8(obs_property_list_item_name(p, i)).contains(cam))
                    obs_data_set_string(st, obs_property_name(p), obs_property_list_item_string(p, i));
            obs_properties_destroy(props);
            const QString scene = e.programScene();
            if (obs_sceneitem_t *item = e.addSource(scene, camType, cam, st)) e.fitItemToCanvas(item, 0.3, "bottom_right");
        }
    }
    if (!plat.isEmpty()) {
        Destination d;
        d.platform = plat;
        d.name = PlatformCatalog::get(plat).name;
        d.server = field("server").toString().trimmed();
        PlatformHub::instance().saveDestination(d, field("key").toString());
    }
    QWizard::accept();
}

} // namespace bc
