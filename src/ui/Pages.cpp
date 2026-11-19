#include "ui/Pages.h"
#include "ui/DeveloperCredentialsDialog.h"
#include "app/Theme.h"
#include "auth/SecureStore.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "overlays/WidgetManager.h"
#include "platforms/PlatformHub.h"
#include "plugins/PluginManager.h"
#include "ui/KawaiiPainter.h"
#include "ui/PropertiesView.h"
#include "ui/StudioDocks.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollBar>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace bc {

// ---------------------------------------------------------------- helpers
QString platformBadge(const QString &id) {
    const PlatformInfo p = PlatformCatalog::get(id);
    return QStringLiteral("<span style='color:%1;font-weight:800'>●</span> %2").arg(p.color.isEmpty() ? "#C96F9D" : p.color, p.name.isEmpty() ? id : p.name);
}

static QLabel *heading(const QString &t, const QString &obj = "H1") { auto *l = new QLabel(t); l->setObjectName(obj); return l; }
static QPushButton *accentButton(const QString &t, const char *accent = "pink") { auto *b = new QPushButton(t); b->setProperty("accent", accent); return b; }
static QString fmtDur(qint64 ms) { const qint64 s = ms / 1000; return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')); }
static void setPill(QLabel *l, const QString &kind, const QString &text) {
    l->setProperty("pill", kind); l->setText(text);
    l->style()->unpolish(l); l->style()->polish(l);
}

// ---------------------------------------------------------------- session history
SessionHistory &SessionHistory::instance() { static SessionHistory h; return h; }

void SessionHistory::sample() {
    auto push = [](QList<double> &l, double v) { l.append(v); while (l.size() > 600) l.removeFirst(); };
    lastCpu = usage.cpuPercent();
    lastGpu = usage.gpuPercent();
    lastRamMB = usage.processMemoryMB();
    auto &e = Engine::instance();
    e.pollOutputStats();
    double kb = 0;
    for (const QString &id : e.activeDestinations()) kb += e.destinationStatus(id).kbps;
    push(cpu, lastCpu); push(gpu, qMax(0.0, lastGpu)); push(kbps, kb); push(fps, e.stats().activeFps);
    if (e.isStreaming() && liveStartMs == 0) { liveStartMs = QDateTime::currentMSecsSinceEpoch(); peakViewers = 0; chatMessages = 0; }
    if (!e.isStreaming()) liveStartMs = 0;
    peakViewers = qMax(peakViewers, PlatformHub::instance().totalViewers());
}

class SparkChart : public QWidget {
public:
    explicit SparkChart(QWidget *p = nullptr) : QWidget(p) { setMinimumHeight(170); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect().adjusted(10, 10, -10, -24);
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 249, 252, 220)); p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 16, 16);
        auto &h = SessionHistory::instance();
        auto line = [&](const QList<double> &d, double maxV, const QColor &c) {
            if (d.size() < 2) return;
            QPainterPath path;
            for (int i = 0; i < d.size(); ++i) {
                const QPointF pt(r.left() + r.width() * i / (d.size() - 1), r.bottom() - r.height() * qBound(0.0, d[i] / maxV, 1.0));
                i ? path.lineTo(pt) : path.moveTo(pt);
            }
            p.setPen(QPen(c, 2.2)); p.setBrush(Qt::NoBrush); p.drawPath(path);
        };
        double maxKb = 1000; for (double v : h.kbps) maxKb = qMax(maxKb, v * 1.15);
        line(h.cpu, 100, theme::BlushPink);
        line(h.gpu, 100, theme::Lavender.darker(120));
        line(h.kbps, maxKb, theme::MintAccent);
        p.setPen(theme::Charcoal);
        p.drawText(QRectF(12, height() - 22, width(), 20), Qt::AlignLeft,
                   QObject::tr("— CPU %   — GPU %   — upload kbps (max %1)   · last 10 min").arg(int(maxKb)));
    }
};

// ===================================================================== Home
HomePage::HomePage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Welcome back, cutie ♡")));
    top->addStretch();
    m_live = new QLabel; m_rec = new QLabel;
    top->addWidget(m_live); top->addWidget(m_rec);
    auto *go = accentButton(tr("✦ Go Live"));
    go->setMinimumHeight(40);
    connect(go, &QPushButton::clicked, this, &HomePage::goLiveRequested);
    top->addWidget(go);
    lay->addLayout(top);

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    const QList<QPair<QString, QString>> tiles = {
        {"stream", tr("Stream")}, {"duration", tr("Duration")}, {"platforms", tr("Connected platforms")},
        {"scene", tr("Current scene")}, {"resolution", tr("Output")}, {"fps", tr("Frame rate")},
        {"bitrate", tr("Upload bitrate")}, {"dropped", tr("Dropped frames")}, {"network", tr("Network")},
        {"cpu", tr("CPU")}, {"gpu", tr("GPU")}, {"ram", tr("Memory (app)")},
        {"recording", tr("Recording")}, {"audio", tr("Microphone")}, {"viewers", tr("Viewers")}};
    for (int i = 0; i < tiles.size(); ++i) {
        auto *box = new QFrame; box->setObjectName("GlassPanel");
        auto *v = new QVBoxLayout(box);
        auto *cap = new QLabel(tiles[i].second); cap->setObjectName("Muted");
        auto *val = new QLabel("—"); val->setObjectName("H2"); val->setTextFormat(Qt::RichText); val->setWordWrap(true);
        v->addWidget(cap); v->addWidget(val);
        m_tiles.insert(tiles[i].first, val);
        grid->addWidget(box, i / 5, i % 5);
    }
    lay->addLayout(grid);

    auto *scenesBox = new QGroupBox(tr("Quick scene switch"));
    m_sceneGrid = new QGridLayout(scenesBox);
    lay->addWidget(scenesBox);
    lay->addStretch();
    connect(&Engine::instance(), &Engine::scenesChanged, this, &HomePage::refresh);
    refresh();
}

void HomePage::refresh() {
    auto &e = Engine::instance();
    auto &h = SessionHistory::instance();
    const bool live = e.isStreaming();
    setPill(m_live, live ? "live" : "off", live ? tr("● LIVE") : tr("Offline"));
    setPill(m_rec, e.isRecording() ? "rec" : "off", e.isRecordingPaused() ? tr("❚❚ Paused") : e.isRecording() ? tr("● REC") : tr("Not recording"));

    QStringList parts; int errors = 0, reconnecting = 0; double kb = 0; int dropped = 0, total = 0;
    for (const auto &d : PlatformHub::instance().destinations()) {
        if (!d.enabled) continue;
        const auto st = e.destinationStatus(d.id);
        kb += st.kbps; dropped += st.droppedFrames; total += st.totalFrames;
        if (st.state == OutputState::Error) ++errors;
        if (st.state == OutputState::Reconnecting) ++reconnecting;
        parts << QStringLiteral("%1 <small>%2</small>").arg(platformBadge(d.platform), outputStateName(st.state));
    }
    m_tiles["stream"]->setText(parts.isEmpty() ? tr("No destinations yet") : parts.join("<br>"));
    m_tiles["duration"]->setText(h.liveStartMs ? fmtDur(QDateTime::currentMSecsSinceEpoch() - h.liveStartMs) : "00:00:00");
    QStringList acc;
    for (auto *a : PlatformHub::instance().accounts()) if (a->isConnected()) acc << platformBadge(a->platformId()) + " " + a->accountName().toHtmlEscaped();
    m_tiles["platforms"]->setText(acc.isEmpty() ? tr("None connected") : acc.join("<br>"));
    m_tiles["scene"]->setText(e.programScene().toHtmlEscaped());
    obs_video_info ovi{}; obs_get_video_info(&ovi);
    m_tiles["resolution"]->setText(QStringLiteral("%1×%2").arg(ovi.output_width).arg(ovi.output_height));
    const auto es = e.stats();
    m_tiles["fps"]->setText(QStringLiteral("%1 fps").arg(es.activeFps, 0, 'f', 1));
    m_tiles["bitrate"]->setText(live ? QStringLiteral("%1 kbps").arg(int(kb)) : "—");
    m_tiles["dropped"]->setText(total ? QStringLiteral("%1 (%2%)").arg(dropped).arg(100.0 * dropped / total, 0, 'f', 2) : "0");
    m_tiles["network"]->setText(!live ? tr("Idle") : errors ? tr("<span style='color:#C9467F'>%n error(s)</span>", nullptr, errors)
                                : reconnecting ? tr("Reconnecting…") : tr("<span style='color:#3E9C7E'>Healthy ♡</span>"));
    m_tiles["cpu"]->setText(QStringLiteral("%1%").arg(h.lastCpu, 0, 'f', 0));
    m_tiles["gpu"]->setText(h.lastGpu < 0 ? tr("n/a") : QStringLiteral("%1%").arg(h.lastGpu, 0, 'f', 0));
    m_tiles["ram"]->setText(QStringLiteral("%1 MB").arg(h.lastRamMB));
    m_tiles["recording"]->setText(e.isRecording() ? tr("Recording ●") : e.isReplayBufferActive() ? tr("Replay buffer on") : tr("Off"));
    obs_source_t *mic = e.micAudio();
    m_tiles["audio"]->setText(!mic ? tr("No mic") : obs_source_muted(mic) ? tr("<span style='color:#C9467F'>Muted</span>") : tr("Active"));
    const int v = PlatformHub::instance().totalViewers();
    m_tiles["viewers"]->setText(v < 0 ? "—" : QString::number(v));

    // scene buttons
    if (m_sceneGrid->count() != e.sceneOrder().size()) {
        while (QLayoutItem *it = m_sceneGrid->takeAt(0)) { delete it->widget(); delete it; }
        int i = 0;
        for (const QString &n : e.sceneOrder()) {
            auto *b = new QPushButton(n);
            b->setCheckable(true);
            connect(b, &QPushButton::clicked, this, [n] { Engine::instance().transitionTo(n); });
            m_sceneGrid->addWidget(b, i / 6, i % 6); ++i;
        }
    }
    for (int i = 0; i < m_sceneGrid->count(); ++i)
        if (auto *b = qobject_cast<QPushButton *>(m_sceneGrid->itemAt(i)->widget())) b->setChecked(b->text() == e.programScene());
}

// ===================================================================== Stream manager
StreamManagerPage::StreamManagerPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Stream Manager")));
    top->addStretch();
    auto *testNet = new QPushButton(tr("Test connections"));
    auto *startAll = accentButton(tr("✦ Start all"));
    auto *stopAll = new QPushButton(tr("Stop all"));
    top->addWidget(testNet); top->addWidget(startAll); top->addWidget(stopAll);
    lay->addLayout(top);

    auto *split = new QSplitter;
    // ---- destinations
    auto *destBox = new QGroupBox(tr("Destinations (multistream)"));
    auto *dl = new QVBoxLayout(destBox);
    m_dest = new QTableWidget(0, 6);
    m_dest->setHorizontalHeaderLabels({tr("On"), tr("Platform"), tr("Name"), tr("Server"), tr("Key"), tr("Status")});
    m_dest->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_dest->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dest->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dest->verticalHeader()->hide();
    dl->addWidget(m_dest, 1);
    auto *drow = new QHBoxLayout;
    auto *add = accentButton(tr("＋ Add destination"), "mint");
    auto *edit = new QPushButton(tr("Edit…"));
    auto *del = new QPushButton(tr("Remove"));
    auto *stopOne = new QPushButton(tr("Stop selected"));
    for (auto *b : {add, edit, del, stopOne}) drow->addWidget(b);
    drow->addStretch();
    dl->addLayout(drow);
    m_bandwidth = new QLabel; m_bandwidth->setWordWrap(true); m_bandwidth->setObjectName("Muted");
    dl->addWidget(m_bandwidth);
    split->addWidget(destBox);

    // ---- accounts + stream info
    auto *right = new QWidget; auto *rl = new QVBoxLayout(right); rl->setContentsMargins(0, 0, 0, 0);
    auto *accBox = new QGroupBox(tr("Connected accounts"));
    m_accounts = new QVBoxLayout(accBox);
    m_deviceCode = new QLabel; m_deviceCode->setWordWrap(true); m_deviceCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(accBox);
    rl->addWidget(m_deviceCode);
    auto *infoBox = new QGroupBox(tr("Stream info"));
    auto *form = new QFormLayout(infoBox);
    m_title = new QLineEdit(Settings::instance().s("stream.title"));
    m_title->setPlaceholderText(tr("cozy stream ♡"));
    m_category = new QLineEdit(Settings::instance().s("stream.category"));
    m_category->setPlaceholderText(tr("e.g. Just Chatting (Twitch) / 20 = Gaming (YouTube category id)"));
    m_desc = new QPlainTextEdit(Settings::instance().s("stream.description"));
    m_desc->setMaximumHeight(90);
    m_privacy = new QComboBox; m_privacy->addItems({"public", "unlisted", "private"});
    m_privacy->setCurrentText(Settings::instance().s("accounts.youtube.privacy").isEmpty() ? "public" : Settings::instance().s("accounts.youtube.privacy"));
    form->addRow(tr("Title"), m_title);
    form->addRow(tr("Category / game"), m_category);
    form->addRow(tr("Description"), m_desc);
    form->addRow(tr("YouTube privacy"), m_privacy);
    auto *push = accentButton(tr("Update on connected platforms"), "mint");
    form->addRow(push);
    rl->addWidget(infoBox);
    rl->addStretch();
    split->addWidget(right);
    split->setStretchFactor(0, 3); split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    auto &hub = PlatformHub::instance();
    connect(add, &QPushButton::clicked, this, [this] { editDestination({}); });
    connect(edit, &QPushButton::clicked, this, [this] { if (m_dest->currentRow() >= 0) editDestination(m_dest->item(m_dest->currentRow(), 2)->data(Qt::UserRole).toString()); });
    connect(m_dest, &QTableWidget::cellDoubleClicked, this, [this](int r) { editDestination(m_dest->item(r, 2)->data(Qt::UserRole).toString()); });
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_dest->currentRow() < 0) return;
        const QString id = m_dest->item(m_dest->currentRow(), 2)->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, tr("Remove destination"), tr("Remove this destination and its saved stream key?")) == QMessageBox::Yes)
            PlatformHub::instance().removeDestination(id);
    });
    connect(stopOne, &QPushButton::clicked, this, [this] {
        if (m_dest->currentRow() >= 0) Engine::instance().stopDestination(m_dest->item(m_dest->currentRow(), 2)->data(Qt::UserRole).toString());
    });
    connect(startAll, &QPushButton::clicked, this, &StreamManagerPage::goLiveRequested);
    connect(stopAll, &QPushButton::clicked, this, [] { PlatformHub::instance().endLive(); });
    connect(testNet, &QPushButton::clicked, this, &StreamManagerPage::testNetwork);
    connect(m_dest, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (c != 0) return;
        const QString id = m_dest->item(r, 2)->data(Qt::UserRole).toString();
        for (auto d : PlatformHub::instance().destinations()) if (d.id == id) { d.enabled = m_dest->item(r, 0)->checkState() == Qt::Checked; PlatformHub::instance().saveDestination(d); }
    });
    connect(push, &QPushButton::clicked, this, [this] {
        auto &s = Settings::instance();
        s.set("stream.title", m_title->text()); s.set("stream.category", m_category->text());
        s.set("stream.description", m_desc->toPlainText()); s.set("accounts.youtube.privacy", m_privacy->currentText());
        s.save();
        int n = 0;
        for (auto *a : PlatformHub::instance().accounts()) {
            if (!a->isConnected()) continue;
            ++n;
            a->updateStreamInfo(m_title->text(), m_category->text(), m_desc->toPlainText(), [a](bool ok, const QString &err) {
                Q_EMIT PlatformHub::instance().notice(ok ? tr("%1 stream info updated ♡").arg(PlatformCatalog::get(a->platformId()).name)
                                                         : tr("%1: %2").arg(PlatformCatalog::get(a->platformId()).name, err), !ok);
            });
        }
        if (!n) QMessageBox::information(this, tr("Stream info"), tr("Saved. Connect Twitch or YouTube to sync title/category automatically — other platforms set this in their own dashboard."));
    });
    connect(&hub, &PlatformHub::destinationsChanged, this, &StreamManagerPage::reloadDestinations);
    connect(&hub, &PlatformHub::accountsChanged, this, &StreamManagerPage::reloadAccounts);
    connect(&hub, &PlatformHub::deviceCode, this, [this](const QString &p, const QString &code, const QString &url) {
        m_deviceCode->setText(tr("<b>%1 sign-in:</b> your browser opened <a href='%2'>%2</a>. Enter the code <b style='font-size:16pt;color:#C96F9D'>%3</b> there and approve HeartCast.")
                              .arg(PlatformCatalog::get(p).name, url, code));
        m_deviceCode->setOpenExternalLinks(true);
    });
    connect(&hub, &PlatformHub::accountsChanged, m_deviceCode, &QLabel::clear);
    connect(&Engine::instance(), &Engine::destinationStateChanged, this, &StreamManagerPage::refreshStatus);
    reloadDestinations();
    reloadAccounts();
}

QString StreamManagerPage::streamTitle() const { return m_title->text(); }

void StreamManagerPage::reloadDestinations() {
    QSignalBlocker b(m_dest);
    const auto dests = PlatformHub::instance().destinations();
    m_dest->setRowCount(int(dests.size()));
    int shared = 0, totalUpload = 0, encodes = 0;
    const int base = Settings::instance().i("output.bitrate") + Settings::instance().i("output.audioBitrate");
    for (int r = 0; r < dests.size(); ++r) {
        const auto &d = dests[r];
        auto *on = new QTableWidgetItem; on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        on->setCheckState(d.enabled ? Qt::Checked : Qt::Unchecked);
        m_dest->setItem(r, 0, on);
        auto *pl = new QTableWidgetItem(PlatformCatalog::get(d.platform).name);
        pl->setForeground(QColor(PlatformCatalog::get(d.platform).color));
        m_dest->setItem(r, 1, pl);
        auto *nm = new QTableWidgetItem(d.name); nm->setData(Qt::UserRole, d.id);
        m_dest->setItem(r, 2, nm);
        m_dest->setItem(r, 3, new QTableWidgetItem(d.server.isEmpty() ? tr("(fetched on Go Live)") : d.server));
        m_dest->setItem(r, 4, new QTableWidgetItem(PlatformHub::instance().hasStreamKey(d) ? tr("saved 🔒") : tr("missing")));
        m_dest->setItem(r, 5, new QTableWidgetItem);
        if (d.enabled) {
            if (d.separateEncoder && d.bitrate > 0) { ++encodes; totalUpload += d.bitrate + 160; }
            else { ++shared; totalUpload += base; }
        }
    }
    if (shared) ++encodes;
    m_bandwidth->setText(dests.isEmpty() ? tr("Add a destination for each platform you want to stream to. You can stream to several at once.")
        : tr("Upload needed ≈ <b>%1 Mbps</b> for %2 destination(s) — keep at least 30% headroom. Encoder sessions: <b>%3</b> "
             "(destinations at the default bitrate share one encode).").arg(totalUpload / 1000.0, 0, 'f', 1).arg(shared + encodes - (shared ? 1 : 0)).arg(encodes));
    m_bandwidth->setTextFormat(Qt::RichText);
    refreshStatus();
}

void StreamManagerPage::refreshStatus() {
    QSignalBlocker b(m_dest);
    for (int r = 0; r < m_dest->rowCount(); ++r) {
        const auto st = Engine::instance().destinationStatus(m_dest->item(r, 2)->data(Qt::UserRole).toString());
        QString t = outputStateName(st.state);
        if (st.state == OutputState::Live) t += QStringLiteral(" · %1 kbps · %2 dropped").arg(int(st.kbps)).arg(st.droppedFrames);
        if (st.state == OutputState::Error) t += ": " + st.error;
        auto *it = m_dest->item(r, 5);
        it->setText(t);
        it->setToolTip(st.error);
        it->setForeground(st.state == OutputState::Live ? QColor("#3E9C7E") : st.state == OutputState::Error ? QColor("#C9467F") : theme::Charcoal);
    }
}

void StreamManagerPage::reloadAccounts() {
    while (QLayoutItem *it = m_accounts->takeAt(0)) { delete it->widget(); delete it; }
    for (const QString id : {"twitch", "youtube"}) {
        PlatformAccount *a = PlatformHub::instance().account(id);
        auto *row = new QWidget; auto *h = new QHBoxLayout(row); h->setContentsMargins(0, 0, 0, 0);
        auto *lbl = new QLabel(platformBadge(id) + (a && a->isConnected() ? " — " + a->accountName().toHtmlEscaped() : tr(" — not connected")));
        h->addWidget(lbl, 1);
        auto *btn = a && a->isConnected() ? new QPushButton(tr("Disconnect")) : accentButton(tr("Connect"), "mint");
        connect(btn, &QPushButton::clicked, this, [this, a, id] {
            if (!a) return;
            if (a->isConnected()) { a->disconnectAccount(); return; }
            if (ensureDevCredentials(id, this)) a->connectAccount();
        });
        h->addWidget(btn);
        m_accounts->addWidget(row);
    }
    auto *note = new QLabel(tr("Kick, TikTok, Facebook, Instagram, X and Trovo don't offer third-party sign-in for live streaming "
                               "(or require app review), so they use your Stream URL + Key. Passwords are never entered here."));
    note->setWordWrap(true); note->setObjectName("Muted");
    m_accounts->addWidget(note);
}

void StreamManagerPage::editDestination(const QString &id) {
    Destination d;
    for (const auto &e : PlatformHub::instance().destinations()) if (e.id == id) d = e;
    QDialog dlg(this);
    dlg.setWindowTitle(id.isEmpty() ? tr("Add destination") : tr("Edit destination"));
    auto *form = new QFormLayout(&dlg);
    auto *plat = new QComboBox;
    for (const auto &p : PlatformCatalog::all()) plat->addItem(p.name, p.id);
    if (!d.platform.isEmpty()) plat->setCurrentIndex(plat->findData(d.platform));
    auto *name = new QLineEdit(d.name);
    auto *server = new QLineEdit(d.server);
    auto *key = new QLineEdit; key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(PlatformHub::instance().hasStreamKey(d) ? tr("(saved — leave empty to keep)") : tr("paste your stream key"));
    auto *showKey = new QCheckBox(tr("Show key"));
    connect(showKey, &QCheckBox::toggled, key, [key](bool on) { key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    auto *sep = new QCheckBox(tr("Use a different bitrate for this destination (extra encoder)"));
    sep->setChecked(d.separateEncoder);
    auto *br = new QSpinBox; br->setRange(500, 50000); br->setSuffix(" kbps"); br->setValue(d.bitrate > 0 ? d.bitrate : 4500);
    auto *help = new QLabel; help->setWordWrap(true); help->setObjectName("Muted");
    auto refreshHelp = [=] {
        const PlatformInfo p = PlatformCatalog::get(plat->currentData().toString());
        QString t = "<b>" + tr("Where to find it:") + "</b> " + p.keyHelp.toHtmlEscaped();
        if (!p.limitations.isEmpty()) t += "<br><b>" + tr("Limitations:") + "</b> " + p.limitations.toHtmlEscaped();
        PlatformAccount *acc = PlatformHub::instance().account(p.id);
        if (acc && acc->isConnected()) t += "<br><span style='color:#3E9C7E'>" + tr("Account connected — server and key are fetched automatically when you go live.") + "</span>";
        t += "<br>" + tr("Recommended: up to %1 kbps, keyframe every %2 s.").arg(p.recommended.value("maxBitrate").toInt()).arg(p.recommended.value("keyint").toInt(2));
        help->setText(t);
        if (server->text().isEmpty() || server->text() == "rtmp://") server->setText(p.defaultServer);
        if (name->text().isEmpty()) name->setText(p.name);
    };
    connect(plat, &QComboBox::currentIndexChanged, &dlg, refreshHelp);
    refreshHelp();
    form->addRow(tr("Platform"), plat);
    form->addRow(tr("Display name"), name);
    form->addRow(tr("Server URL"), server);
    form->addRow(tr("Stream key"), key);
    form->addRow(QString(), showKey);
    form->addRow(QString(), sep);
    form->addRow(tr("Destination bitrate"), br);
    form->addRow(help);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, [&] {
        const QString s = server->text().trimmed();
        PlatformAccount *acc = PlatformHub::instance().account(plat->currentData().toString());
        const bool auto_ = acc && acc->isConnected();
        if (!auto_ && !(s.startsWith("rtmp://") || s.startsWith("rtmps://") || s.startsWith("srt://"))) {
            QMessageBox::warning(&dlg, tr("Destination"), tr("Server URL must start with rtmp://, rtmps:// or srt://")); return;
        }
        dlg.accept();
    });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    d.platform = plat->currentData().toString();
    d.name = name->text().trimmed().isEmpty() ? plat->currentText() : name->text().trimmed();
    d.server = server->text().trimmed();
    d.separateEncoder = sep->isChecked();
    d.bitrate = br->value();
    PlatformHub::instance().saveDestination(d, key->text());
}

void StreamManagerPage::testNetwork() {
    QStringList report;
    for (const auto &d : PlatformHub::instance().destinations()) {
        const QUrl u(d.server);
        if (!u.isValid() || u.host().isEmpty()) { report << tr("%1: no server URL yet").arg(d.name); continue; }
        QTcpSocket s;
        QElapsedTimer t; t.start();
        const int port = u.port(u.scheme() == "rtmps" ? 443 : 1935);
        s.connectToHost(u.host(), quint16(port));
        const bool ok = s.waitForConnected(4000);
        report << (ok ? tr("%1: ✓ %2:%3 reachable in %4 ms").arg(d.name, u.host()).arg(port).arg(t.elapsed())
                      : tr("%1: ✗ cannot reach %2:%3 (%4)").arg(d.name, u.host()).arg(port).arg(s.errorString()));
        s.abort();
    }
    QMessageBox::information(this, tr("Connection test"), report.isEmpty() ? tr("No destinations to test.") : report.join('\n'));
}

// ===================================================================== Recording
RecordingPage::RecordingPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Recording studio")));
    top->addStretch();
    m_status = new QLabel; top->addWidget(m_status);
    auto *rec = accentButton(tr("● Record"));
    auto *pause = new QPushButton(tr("❚❚ Pause"));
    auto *replay = new QPushButton(tr("Replay buffer"));
    auto *save = accentButton(tr("Save replay ✦"), "mint");
    for (auto *b : {rec, pause, replay, save}) top->addWidget(b);
    lay->addLayout(top);
    m_info = new QLabel; m_info->setObjectName("Muted"); m_info->setWordWrap(true);
    lay->addWidget(m_info);
    m_lib = new QListWidget;
    m_lib->setViewMode(QListView::IconMode);
    m_lib->setIconSize(QSize(240, 135));
    m_lib->setGridSize(QSize(270, 200));
    m_lib->setResizeMode(QListView::Adjust);
    m_lib->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(m_lib, 1);
    auto *row = new QHBoxLayout;
    auto *openF = new QPushButton(tr("Open folder"));
    auto *refresh = new QPushButton(tr("Refresh"));
    row->addWidget(openF); row->addWidget(refresh); row->addStretch();
    lay->addLayout(row);

    auto &e = Engine::instance();
    connect(rec, &QPushButton::clicked, this, [this] {
        auto &e = Engine::instance();
        QString err;
        if (e.isRecording()) e.stopRecording();
        else if (!e.startRecording(&err)) QMessageBox::warning(this, tr("Recording"), err);
    });
    connect(pause, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.pauseRecording(!e.isRecordingPaused()); });
    connect(replay, &QPushButton::clicked, this, [this] {
        auto &e = Engine::instance(); QString err;
        if (e.isReplayBufferActive()) e.stopReplayBuffer();
        else if (!e.startReplayBuffer(&err)) QMessageBox::warning(this, tr("Replay buffer"), err);
    });
    connect(save, &QPushButton::clicked, this, [this] { if (!Engine::instance().saveReplay()) QMessageBox::information(this, tr("Replay"), tr("Start the replay buffer first ♡")); });
    connect(openF, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::instance().s("recording.path"))); });
    connect(refresh, &QPushButton::clicked, this, &RecordingPage::reloadLibrary);
    connect(&e, &Engine::recordingStateChanged, this, [this] { refreshStatus(); QTimer::singleShot(1500, this, &RecordingPage::reloadLibrary); });
    connect(&e, &Engine::replayBufferStateChanged, this, &RecordingPage::refreshStatus);
    connect(&e, &Engine::replaySaved, this, [this] { reloadLibrary(); });
    connect(m_lib, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *it) { QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString())); });
    connect(m_lib, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        auto *it = m_lib->itemAt(pt);
        if (!it) return;
        const QString path = it->data(Qt::UserRole).toString();
        QMenu m(this);
        m.addAction(tr("Play"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        m.addAction(tr("Show in folder"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
        m.addAction(tr("Rename…"), this, [this, path] {
            const QFileInfo fi(path);
            const QString n = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, fi.completeBaseName());
            if (!n.isEmpty() && !QFile::rename(path, fi.absolutePath() + "/" + n + "." + fi.suffix())) QMessageBox::warning(this, tr("Rename"), tr("Could not rename the file."));
            reloadLibrary();
        });
        m.addAction(tr("Export copy…"), this, [this, path] {
            const QString to = QFileDialog::getSaveFileName(this, tr("Export"), QFileInfo(path).fileName());
            if (!to.isEmpty() && !QFile::copy(path, to)) QMessageBox::warning(this, tr("Export"), tr("Copy failed."));
        });
        if (path.endsWith(".mkv")) m.addAction(tr("Remux to MP4"), this, [this, path] { Engine::instance().remuxToMp4(path); QTimer::singleShot(4000, this, &RecordingPage::reloadLibrary); });
        m.addSeparator();
        m.addAction(tr("Move to Recycle Bin"), this, [this, path] {
            if (QMessageBox::question(this, tr("Delete"), tr("Move %1 to the Recycle Bin?").arg(QFileInfo(path).fileName())) == QMessageBox::Yes) QFile::moveToTrash(path);
            reloadLibrary();
        });
        m.exec(m_lib->viewport()->mapToGlobal(pt));
    });
    refreshStatus();
    reloadLibrary();
}

void RecordingPage::refreshStatus() {
    auto &e = Engine::instance();
    auto &s = Settings::instance();
    setPill(m_status, e.isRecording() ? "rec" : e.isReplayBufferActive() ? "ok" : "off",
            e.isRecordingPaused() ? tr("❚❚ Paused") : e.isRecording() ? tr("● Recording") : e.isReplayBufferActive() ? tr("Replay buffer on") : tr("Idle"));
    m_info->setText(tr("Saving to <b>%1</b> as <b>%2</b> (quality: %3, %4 audio track(s)). MKV is crash-safe; %5. Replay buffer keeps the last %6 s.")
        .arg(s.s("recording.path").toHtmlEscaped(), s.s("recording.format").toUpper(), s.s("recording.quality")).arg(s.i("recording.tracks"))
        .arg(s.b("recording.remuxToMp4") ? tr("an MP4 copy is made automatically when you stop") : tr("auto-remux is off"))
        .arg(s.i("recording.replayBufferSec")));
}

void RecordingPage::reloadLibrary() {
    m_lib->clear();
    const QString dir = Settings::instance().s("recording.path");
    const QString thumbs = Paths::ensure(Paths::cache() + "/thumbs");
    QDir d(dir);
    const auto files = d.entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.ts"}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files) {
        auto *it = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 MB").arg(fi.fileName(), fi.lastModified().toString("MMM d, HH:mm"))
                                       .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        const QString thumb = thumbs + "/" + QString::number(qHash(fi.absoluteFilePath() + QString::number(fi.size()))) + ".jpg";
        QPixmap pm(thumb);
        if (pm.isNull()) {
            pm = QPixmap(240, 135); pm.fill(theme::BabyPink);
            QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(theme::CottonCandy);
            p.drawPath(kawaii::heart(QPointF(120, 70), 50)); p.end();
            // Grab a real frame asynchronously with Qt Multimedia (no external tools).
            auto *player = new QMediaPlayer(this);
            auto *sink = new QVideoSink(player);
            player->setVideoSink(sink);
            player->setSource(QUrl::fromLocalFile(fi.absoluteFilePath()));
            QPointer<QListWidget> list(m_lib);
            const QString abs = fi.absoluteFilePath();
            connect(sink, &QVideoSink::videoFrameChanged, player, [player, thumb, list, abs](const QVideoFrame &f) {
                const QImage img = f.toImage();
                if (img.isNull()) return;
                const QImage sc = img.scaled(240, 135, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                sc.save(thumb, "JPG", 85);
                if (list) for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->data(Qt::UserRole).toString() == abs) list->item(i)->setIcon(QPixmap::fromImage(sc));
                player->stop(); player->deleteLater();
            });
            connect(player, &QMediaPlayer::errorOccurred, player, &QObject::deleteLater);
            player->setAudioOutput(nullptr);
            player->play();
            QTimer::singleShot(8000, player, &QObject::deleteLater);
        }
        it->setIcon(pm);
        m_lib->addItem(it);
    }
}

// ===================================================================== Media library
MediaLibraryPage::MediaLibraryPage(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Media Library")));
    top->addStretch();
    m_search = new QLineEdit; m_search->setPlaceholderText(tr("Search names & tags…"));
    m_filter = new QComboBox; m_filter->addItems({tr("All"), tr("Images & GIFs"), tr("Videos"), tr("Audio")});
    auto *imp = accentButton(tr("Import…"), "mint");
    auto *newFolder = new QPushButton(tr("New folder"));
    top->addWidget(m_search); top->addWidget(m_filter); top->addWidget(newFolder); top->addWidget(imp);
    lay->addLayout(top);
    auto *split = new QSplitter;
    m_folders = new QListWidget; m_folders->setMaximumWidth(220);
    m_files = new QListWidget;
    m_files->setViewMode(QListView::IconMode); m_files->setIconSize(QSize(128, 96)); m_files->setGridSize(QSize(150, 140));
    m_files->setResizeMode(QListView::Adjust); m_files->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *side = new QWidget; auto *sl = new QVBoxLayout(side);
    m_preview = new QLabel; m_preview->setMinimumSize(280, 180); m_preview->setAlignment(Qt::AlignCenter);
    m_meta = new QLabel; m_meta->setWordWrap(true); m_meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *addBtn = accentButton(tr("Add to current scene"));
    auto *tagBtn = new QPushButton(tr("Edit tags…"));
    sl->addWidget(m_preview); sl->addWidget(m_meta); sl->addWidget(addBtn); sl->addWidget(tagBtn); sl->addStretch();
    split->addWidget(m_folders); split->addWidget(m_files); split->addWidget(side);
    split->setStretchFactor(1, 3);
    lay->addWidget(split, 1);
    auto *hint = new QLabel(tr("Drag files from Explorer onto this page to import them. Double-click to add to the current scene."));
    hint->setObjectName("Muted"); lay->addWidget(hint);

    connect(imp, &QPushButton::clicked, this, [this] {
        importFiles(QFileDialog::getOpenFileNames(this, tr("Import media"), QString(),
            tr("Media (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.mp4 *.mkv *.mov *.webm *.mp3 *.wav *.ogg *.flac);;All files (*)")));
    });
    connect(newFolder, &QPushButton::clicked, this, [this] {
        const QString n = QInputDialog::getText(this, tr("New folder"), tr("Folder name:"));
        if (!n.trimmed().isEmpty()) { QDir(Paths::mediaLibrary()).mkpath(n.trimmed()); reload(); }
    });
    connect(m_search, &QLineEdit::textChanged, this, &MediaLibraryPage::reload);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &MediaLibraryPage::reload);
    connect(m_folders, &QListWidget::currentRowChanged, this, &MediaLibraryPage::reload);
    connect(m_files, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        if (!it) return;
        const QFileInfo fi(it->data(Qt::UserRole).toString());
        QPixmap pm(fi.absoluteFilePath());
        m_preview->setPixmap(pm.isNull() ? QPixmap() : pm.scaled(280, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (pm.isNull()) m_preview->setText(tr("▶ %1").arg(fi.suffix().toUpper()));
        const QString rel = QDir(Paths::mediaLibrary()).relativeFilePath(fi.absoluteFilePath());
        m_meta->setText(tr("<b>%1</b><br>%2 KB · %3<br>%4<br>Tags: %5").arg(fi.fileName().toHtmlEscaped()).arg(fi.size() / 1024)
            .arg(fi.lastModified().toString("yyyy-MM-dd HH:mm"), pm.isNull() ? QString() : QStringLiteral("%1×%2 px").arg(pm.width()).arg(pm.height()),
                 tagsFor(rel).join(", ").toHtmlEscaped()));
    });
    connect(m_files, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { addToScene(it->data(Qt::UserRole).toString()); });
    connect(addBtn, &QPushButton::clicked, this, [this] { if (auto *it = m_files->currentItem()) addToScene(it->data(Qt::UserRole).toString()); });
    connect(tagBtn, &QPushButton::clicked, this, [this] {
        auto *it = m_files->currentItem(); if (!it) return;
        const QString rel = QDir(Paths::mediaLibrary()).relativeFilePath(it->data(Qt::UserRole).toString());
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Tags"), tr("Comma-separated tags:"), QLineEdit::Normal, tagsFor(rel).join(", "), &ok);
        if (ok) { QStringList l; for (const QString &x : t.split(',', Qt::SkipEmptyParts)) l << x.trimmed(); setTags(rel, l); reload(); }
    });
    connect(m_files, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        auto *it = m_files->itemAt(pt); if (!it) return;
        const QString path = it->data(Qt::UserRole).toString();
        QMenu m(this);
        m.addAction(tr("Add to current scene"), this, [this, path] { addToScene(path); });
        m.addAction(tr("Open"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        m.addAction(tr("Show in folder"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
        m.addAction(tr("Move to Recycle Bin"), this, [this, path] { QFile::moveToTrash(path); reload(); });
        m.exec(m_files->viewport()->mapToGlobal(pt));
    });
    ensureStarterAssets();
    reload();
}

void MediaLibraryPage::ensureStarterAssets() {
    const QString dir = Paths::ensure(Paths::mediaLibrary() + "/Starter ♡");
    if (QFile::exists(dir + "/heart-sticker.png")) return;
    // Original starter assets painted procedurally (no third-party artwork).
    auto save = [&](const QString &name, QSize sz, const std::function<void(QPainter &, QSize)> &fn) {
        QImage img(sz, QImage::Format_ARGB32); img.fill(Qt::transparent);
        QPainter p(&img); p.setRenderHints(QPainter::Antialiasing); fn(p, sz); p.end();
        img.save(dir + "/" + name);
    };
    save("heart-sticker.png", {512, 512}, [](QPainter &p, QSize s) {
        kawaii::drawGlossyHeart(p, QPointF(s.width() / 2.0, s.height() * 0.6), 330, theme::PastelRose, theme::CottonCandy, theme::DeepRose);
        p.setPen(QPen(theme::MintAccent.darker(120), 5)); p.setBrush(theme::PastelMint);
        p.drawPath(kawaii::bow(QPointF(s.width() / 2.0, s.height() * 0.3), 90));
    });
    save("heart-frame.png", {1280, 720}, [](QPainter &p, QSize s) {
        p.setPen(QPen(theme::CottonCandy, 18)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(20, 20, s.width() - 40, s.height() - 40), 48, 48);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 14; ++i) { p.setBrush(i % 2 ? theme::PastelMint : theme::BlushPink); p.drawPath(kawaii::heart(QPointF(60 + i * 90, 30), 40)); p.drawPath(kawaii::heart(QPointF(60 + i * 90, s.height() - 30), 40)); }
    });
    save("sparkle-corner.png", {400, 400}, [](QPainter &p, QSize) {
        p.setPen(Qt::NoPen);
        const QColor cs[] = {theme::Lavender, theme::CottonCandy, theme::PastelMint, theme::PearlWhite};
        for (int i = 0; i < 12; ++i) { p.setBrush(cs[i % 4]); p.drawPath(kawaii::sparkle(QPointF(30 + (i * 53) % 340, 30 + (i * 97) % 340), 14 + (i % 4) * 8)); }
    });
    save("pastel-background.png", {1920, 1080}, [](QPainter &p, QSize s) {
        QLinearGradient g(0, 0, s.width(), s.height()); g.setColorAt(0, theme::BabyPink); g.setColorAt(0.5, theme::SoftLilac); g.setColorAt(1, theme::SoftMint);
        p.fillRect(QRect(QPoint(), s), g);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 60; ++i) { QColor c = theme::PearlWhite; c.setAlpha(120); p.setBrush(c); p.drawPath(kawaii::star(QPointF((i * 331) % s.width(), (i * 197) % s.height()), 10 + i % 12, 4 + i % 5)); }
    });
    save("ribbon-banner.png", {1200, 220}, [](QPainter &p, QSize s) {
        kawaii::drawGlossyCard(p, QRectF(40, 30, s.width() - 80, s.height() - 60), theme::PastelRose, theme::CottonCandy, theme::DeepRose, 40);
        p.setPen(QPen(theme::MintAccent, 3)); p.setBrush(theme::PastelMint); p.drawPath(kawaii::bow(QPointF(s.width() / 2.0, 34), 60));
    });
}

void MediaLibraryPage::reload() {
    const QString root = Paths::mediaLibrary();
    const QString curFolder = m_folders->currentItem() ? m_folders->currentItem()->data(Qt::UserRole).toString() : QString();
    {
        QSignalBlocker b(m_folders);
        m_folders->clear();
        auto *all = new QListWidgetItem(tr("All media")); all->setData(Qt::UserRole, QString()); m_folders->addItem(all);
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            auto *f = new QListWidgetItem("📁 " + QDir(root).relativeFilePath(p)); f->setData(Qt::UserRole, p); m_folders->addItem(f);
            if (p == curFolder) m_folders->setCurrentItem(f);
        }
        if (!m_folders->currentItem()) m_folders->setCurrentRow(0);
    }
    const QString base = m_folders->currentItem()->data(Qt::UserRole).toString();
    const QStringList img = {"png", "jpg", "jpeg", "gif", "webp", "bmp"}, vid = {"mp4", "mkv", "mov", "webm", "flv"}, aud = {"mp3", "wav", "ogg", "flac", "m4a"};
    m_files->clear();
    QDirIterator it(base.isEmpty() ? root : base, QDir::Files, base.isEmpty() ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    const QString q = m_search->text().trimmed().toLower();
    while (it.hasNext()) {
        const QFileInfo fi(it.next());
        const QString ext = fi.suffix().toLower();
        const int f = m_filter->currentIndex();
        if ((f == 1 && !img.contains(ext)) || (f == 2 && !vid.contains(ext)) || (f == 3 && !aud.contains(ext))) continue;
        if (!img.contains(ext) && !vid.contains(ext) && !aud.contains(ext)) continue;
        const QString rel = QDir(root).relativeFilePath(fi.absoluteFilePath());
        if (!q.isEmpty() && !fi.fileName().toLower().contains(q) && !tagsFor(rel).join(' ').toLower().contains(q)) continue;
        auto *item = new QListWidgetItem(fi.fileName());
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        QPixmap pm;
        if (img.contains(ext)) pm = QPixmap(fi.absoluteFilePath()).scaled(128, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (pm.isNull()) {
            pm = QPixmap(128, 96); pm.fill(vid.contains(ext) ? theme::SoftLilac : theme::SoftMint);
            QPainter p(&pm); p.setPen(theme::DeepRose); QFont fnt("Nunito"); fnt.setPixelSize(28); p.setFont(fnt);
            p.drawText(pm.rect(), Qt::AlignCenter, vid.contains(ext) ? "▶" : "♪");
        }
        item->setIcon(pm);
        m_files->addItem(item);
    }
}

void MediaLibraryPage::importFiles(const QStringList &files) {
    const QString dest = m_folders->currentItem() && !m_folders->currentItem()->data(Qt::UserRole).toString().isEmpty()
                       ? m_folders->currentItem()->data(Qt::UserRole).toString() : Paths::ensure(Paths::mediaLibrary() + "/Imported");
    int n = 0;
    for (const QString &f : files) {
        QString target = dest + "/" + QFileInfo(f).fileName();
        for (int i = 2; QFile::exists(target); ++i) target = dest + "/" + QFileInfo(f).completeBaseName() + QStringLiteral(" (%1).").arg(i) + QFileInfo(f).suffix();
        if (QFile::copy(f, target)) ++n;
    }
    reload();
    if (n) Q_EMIT PlatformHub::instance().notice(tr("Imported %n file(s) ♡", nullptr, n), false);
}

void MediaLibraryPage::addToScene(const QString &path) {
    auto &e = Engine::instance();
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool image = QStringList{"png", "jpg", "jpeg", "webp", "bmp"}.contains(ext);
    OBSDataAutoRelease st = obs_data_create();
    if (image) obs_data_set_string(st, "file", path.toUtf8().constData());
    else { obs_data_set_string(st, "local_file", path.toUtf8().constData()); obs_data_set_bool(st, "looping", ext == "gif" || ext == "webm"); }
    const QString scene = e.studioMode() ? e.previewScene() : e.programScene();
    if (e.addSource(scene, image ? "image_source" : "ffmpeg_source", QFileInfo(path).completeBaseName(), st))
        Q_EMIT PlatformHub::instance().notice(tr("Added to \"%1\" ✦").arg(scene), false);
}

QStringList MediaLibraryPage::tagsFor(const QString &rel) const {
    QFile f(Paths::mediaLibrary() + "/library.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(rel).toVariant().toStringList();
}
void MediaLibraryPage::setTags(const QString &rel, const QStringList &tags) {
    QFile f(Paths::mediaLibrary() + "/library.json");
    QJsonObject o;
    if (f.open(QIODevice::ReadOnly)) { o = QJsonDocument::fromJson(f.readAll()).object(); f.close(); }
    o[rel] = QJsonArray::fromStringList(tags);
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson());
}
void MediaLibraryPage::dragEnterEvent(QDragEnterEvent *e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MediaLibraryPage::dropEvent(QDropEvent *e) {
    QStringList files;
    for (const QUrl &u : e->mimeData()->urls()) if (u.isLocalFile()) files << u.toLocalFile();
    importFiles(files);
}

// ===================================================================== Widgets & Overlays
WidgetsPage::WidgetsPage(SourcesPanel *sources, bool overlays, QWidget *parent) : QWidget(parent), m_sources(sources), m_overlays(overlays) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(overlays ? tr("Overlay packs") : tr("Widgets & alerts")));
    top->addStretch();
    top->addWidget(new QLabel(tr("Pack:")));
    m_pack = new QComboBox;
    for (const auto &p : WidgetManager::instance().packs()) m_pack->addItem(p.emoji + " " + p.name, p.id);
    top->addWidget(m_pack);
    lay->addLayout(top);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget; m_grid = new QGridLayout(host); m_grid->setSpacing(14);
    scroll->setWidget(host);
    lay->addWidget(scroll, 2);
    connect(m_pack, &QComboBox::currentIndexChanged, this, &WidgetsPage::rebuildGallery);

    if (!overlays) {
        auto *alerts = new QGroupBox(tr("Alert rules && tests"));
        auto *al = new QGridLayout(alerts);
        al->addWidget(new QLabel(tr("<b>Event</b>")), 0, 0); al->addWidget(new QLabel(tr("<b>On</b>")), 0, 1);
        al->addWidget(new QLabel(tr("<b>Min amount</b>")), 0, 2); al->addWidget(new QLabel(tr("<b>Message ({user} {amount} {message})</b>")), 0, 3);
        int r = 1;
        for (const QString &t : WidgetManager::alertTypes()) {
            const AlertRule rule = WidgetManager::instance().rule(t);
            auto *on = new QCheckBox; on->setChecked(rule.enabled);
            auto *min = new QSpinBox; min->setRange(0, 1000000); min->setValue(int(rule.minAmount));
            auto *tpl = new QLineEdit(rule.templ);
            auto *test = new QPushButton(tr("Test ✦"));
            auto save = [t, on, min, tpl] { WidgetManager::instance().setRule(t, AlertRule{on->isChecked(), double(min->value()), tpl->text()}); };
            connect(on, &QCheckBox::toggled, this, save);
            connect(min, &QSpinBox::valueChanged, this, save);
            connect(tpl, &QLineEdit::editingFinished, this, save);
            connect(test, &QPushButton::clicked, this, [t] { PlatformHub::instance().emitTestEvent(t); });
            al->addWidget(new QLabel(t), r, 0); al->addWidget(on, r, 1); al->addWidget(min, r, 2); al->addWidget(tpl, r, 3); al->addWidget(test, r, 4);
            ++r;
        }
        auto *restart = new QPushButton(tr("Restart countdowns && stopwatches"));
        connect(restart, &QPushButton::clicked, this, [] { WidgetManager::instance().restartTimers(); });
        al->addWidget(restart, r, 0, 1, 2);
        auto *note = new QLabel(tr("Alert sound, duration, size and look are set per Alert box widget (double-click it in Sources). "
                                   "Animation, font and colours come from the overlay pack."));
        note->setWordWrap(true); note->setObjectName("Muted");
        al->addWidget(note, r + 1, 0, 1, 5);
        lay->addWidget(alerts, 1);
    }
    rebuildGallery();
}

void WidgetsPage::rebuildGallery() {
    while (QLayoutItem *it = m_grid->takeAt(0)) { delete it->widget(); delete it; }
    const QString pack = m_pack->currentData().toString();
    if (m_overlays) {
        int i = 0;
        for (const auto &p : WidgetManager::instance().packs()) {
            auto *card = new QFrame; card->setObjectName("GlassPanel");
            auto *v = new QVBoxLayout(card);
            WidgetConfig c; c.kind = "text_panel"; c.pack = p.id; c.title = p.emoji + " " + p.name; c.fontSize = 34;
            auto *img = new QLabel; img->setPixmap(QPixmap::fromImage(WidgetManager::instance().renderPreview(c, {360, 120})));
            v->addWidget(img);
            auto *apply = new QPushButton(tr("Create \"%1\" scene").arg(p.name));
            connect(apply, &QPushButton::clicked, this, [this, p] {
                auto &e = Engine::instance();
                const QString scene = e.createScene(tr("%1 ♡").arg(p.name));
                OBSDataAutoRelease g = obs_data_create();
                obs_data_set_string(g, "bc_c1", p.bg1.name().toUtf8().constData());
                obs_data_set_string(g, "bc_c2", p.bg2.name().toUtf8().constData());
                e.addSource(scene, "bc_gradient", tr("%1 background").arg(p.name), g);
                e.setPreviewScene(scene);
                for (const QString k : {"text_panel", "label", "goal", "chat", "alerts"}) m_sources->addWidgetOfKind(k, p.id);
                Q_EMIT PlatformHub::instance().notice(tr("Created scene \"%1\" with the %2 pack ♡").arg(scene, p.name), false);
            });
            v->addWidget(apply);
            m_grid->addWidget(card, i / 3, i % 3); ++i;
        }
        return;
    }
    const QList<QPair<QString, QString>> kinds = {{"alerts", tr("Alert box")}, {"chat", tr("Chat box")}, {"events", tr("Event list")},
        {"label", tr("Stream label")}, {"goal", tr("Goal bar")}, {"countdown", tr("Countdown")}, {"stopwatch", tr("Stopwatch")},
        {"social", tr("Social panel")}, {"sponsor", tr("Sponsor panel")}, {"viewers", tr("Viewer count")}, {"text_panel", tr("Text panel")}};
    int i = 0;
    for (const auto &k : kinds) {
        auto *card = new QFrame; card->setObjectName("GlassPanel");
        auto *v = new QVBoxLayout(card);
        WidgetConfig c; c.kind = k.first; c.pack = pack; c.fontSize = 22; c.goalTarget = 50; c.countdownMinutes = 5;
        c.socials = "twitch:yourname;tiktok:yourname"; c.alertSeconds = 6;
        auto *img = new QLabel; img->setPixmap(QPixmap::fromImage(WidgetManager::instance().renderPreview(c, {300, 110})));
        v->addWidget(new QLabel("<b>" + k.second + "</b>"));
        v->addWidget(img);
        auto *add = new QPushButton(tr("Add to scene ＋"));
        connect(add, &QPushButton::clicked, this, [this, k, pack] { m_sources->addWidgetOfKind(k.first, pack); });
        v->addWidget(add);
        m_grid->addWidget(card, i / 4, i % 4); ++i;
    }
}

// ===================================================================== Effects
EffectsPage::EffectsPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(heading(tr("Camera & video effects")));
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Source:")));
    m_src = new QComboBox; row->addWidget(m_src, 1);
    auto *open = accentButton(tr("Open filters && effects"));
    auto *refresh = new QPushButton(tr("Refresh list"));
    row->addWidget(open); row->addWidget(refresh);
    lay->addLayout(row);
    auto *quick = new QGroupBox(tr("Quick add"));
    auto *ql = new QGridLayout(quick);
    const QList<QPair<QString, QString>> fx = {
        {"color_filter_v2", tr("Colour correction (brightness, contrast, saturation, hue, gamma)")},
        {"chroma_key_filter_v2", tr("Chroma key / green screen")}, {"color_key_filter_v2", tr("Colour key")},
        {"sharpness_filter_v2", tr("Sharpen")}, {"crop_filter", tr("Crop / pad")}, {"mask_filter_v2", tr("Image mask / blend")},
        {"clut_filter", tr("LUT (.cube / .png)")}, {"scale_filter", tr("Scaling / aspect ratio")},
        {"noise_suppress_filter_v2", tr("Noise suppression (mic)")}, {"noise_gate_filter", tr("Noise gate (mic)")},
        {"compressor_filter", tr("Compressor (mic)")}, {"basic_eq_filter", tr("3-band EQ (mic)")}};
    int i = 0;
    for (const auto &f : fx) {
        auto *b = new QPushButton(f.second);
        connect(b, &QPushButton::clicked, this, [this, f] {
            OBSSourceAutoRelease src = obs_get_source_by_name(m_src->currentText().toUtf8().constData());
            if (!src) return;
            OBSSourceAutoRelease filt = obs_source_create(f.first.toUtf8().constData(), f.second.section(" (", 0, 0).toUtf8().constData(), nullptr, nullptr);
            if (!filt) { QMessageBox::warning(this, tr("Effects"), tr("This effect isn't available in your build.")); return; }
            obs_source_filter_add(src, filt);
            (new FiltersDialog(src, this))->show();
        });
        ql->addWidget(b, i / 2, i % 2); ++i;
    }
    lay->addWidget(quick);
    auto *ai = new QGroupBox(tr("AI background removal"));
    auto *al = new QVBoxLayout(ai);
    m_aiStatus = new QLabel; m_aiStatus->setWordWrap(true);
    al->addWidget(m_aiStatus);
    auto *aiBtn = accentButton(tr("Add background removal to selected source"), "mint");
    al->addWidget(aiBtn);
    lay->addWidget(ai);
    lay->addStretch();

    auto aiFilter = [] {
        for (const auto &f : Engine::instance().filterTypes(false))
            if (f.id == "nvidia_greenscreen_filter" || f.id == "background_removal") return f.id;
        return QString();
    };
    const QString aid = aiFilter();
    m_aiStatus->setText(aid.isEmpty()
        ? tr("Not available yet: install either the <b>NVIDIA Broadcast / Video Effects SDK</b> runtime (RTX GPUs, GPU-accelerated) "
             "or the open-source <b>obs-backgroundremoval</b> plugin (ONNX, GPU with CPU fallback) into HeartCast's plugin folder, then restart.")
        : tr("✓ Available via <b>%1</b>.").arg(aid));
    aiBtn->setEnabled(!aid.isEmpty());
    connect(aiBtn, &QPushButton::clicked, this, [this, aid] {
        OBSSourceAutoRelease src = obs_get_source_by_name(m_src->currentText().toUtf8().constData());
        if (!src) return;
        OBSSourceAutoRelease filt = obs_source_create(aid.toUtf8().constData(), "Background removal", nullptr, nullptr);
        if (filt) { obs_source_filter_add(src, filt); (new FiltersDialog(src, this))->show(); }
    });
    connect(open, &QPushButton::clicked, this, [this] {
        OBSSourceAutoRelease src = obs_get_source_by_name(m_src->currentText().toUtf8().constData());
        if (src) (new FiltersDialog(src, this))->show();
    });
    connect(refresh, &QPushButton::clicked, this, &EffectsPage::reloadSources);
    connect(&Engine::instance(), &Engine::sceneItemsChanged, this, &EffectsPage::reloadSources);
    reloadSources();
}

void EffectsPage::reloadSources() {
    const QString cur = m_src->currentText();
    m_src->clear();
    obs_enum_sources([](void *p, obs_source_t *s) {
        if (obs_source_get_output_flags(s) & (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO))
            static_cast<QComboBox *>(p)->addItem(QString::fromUtf8(obs_source_get_name(s)));
        return true;
    }, m_src);
    m_src->setCurrentText(cur);
}

// ===================================================================== Transitions
TransitionsPage::TransitionsPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(heading(tr("Transitions")));
    auto *split = new QSplitter;
    auto *left = new QWidget; auto *ll = new QVBoxLayout(left);
    m_list = new QListWidget;
    const QList<QPair<QString, QString>> builtins = {
        {"bc_luma_heart", tr("♡ Heart Iris (kawaii)")}, {"bc_luma_star", tr("★ Star Burst (kawaii)")},
        {"bc_luma_ribbon", tr("🎀 Ribbon Sweep (kawaii)")}, {"bc_luma_sparkle", tr("✦ Sparkle Dissolve (kawaii)")},
        {"cut_transition", tr("Cut")}, {"fade_transition", tr("Fade")}, {"fade_to_color_transition", tr("Fade to colour")},
        {"swipe_transition", tr("Swipe")}, {"slide_transition", tr("Slide")}, {"wipe_transition", tr("Luma wipe")},
        {"obs_stinger_transition", tr("Stinger (video)")}};
    const QStringList avail = Engine::instance().transitionTypes();
    for (const auto &b : builtins) {
        if (!b.first.startsWith("bc_") && !avail.contains(b.first)) continue;
        if (b.first.startsWith("bc_") && !avail.contains("wipe_transition")) continue;
        auto *it = new QListWidgetItem(b.second); it->setData(Qt::UserRole, b.first); m_list->addItem(it);
    }
    ll->addWidget(m_list, 1);
    auto *dRow = new QHBoxLayout;
    dRow->addWidget(new QLabel(tr("Duration")));
    m_duration = new QSpinBox; m_duration->setRange(50, 20000); m_duration->setSuffix(" ms"); m_duration->setValue(Engine::instance().transitionDurationMs());
    dRow->addWidget(m_duration);
    ll->addLayout(dRow);
    auto *test = accentButton(tr("Preview transition ✦"), "mint");
    ll->addWidget(test);
    split->addWidget(left);
    m_propsHost = new QWidget; new QVBoxLayout(m_propsHost);
    split->addWidget(m_propsHost);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        Engine::instance().setTransition(it->data(Qt::UserRole).toString());
        showCurrent();
    });
    connect(m_duration, &QSpinBox::valueChanged, this, [](int v) { Engine::instance().setTransitionDurationMs(v); });
    connect(test, &QPushButton::clicked, this, [] {
        auto &e = Engine::instance();
        const QStringList order = e.sceneOrder();
        if (order.size() < 2) return;
        const QString cur = e.programScene();
        const QString other = order.at((order.indexOf(cur) + 1) % order.size());
        e.transitionTo(other);
        QTimer::singleShot(e.transitionDurationMs() + 900, [cur] { Engine::instance().transitionTo(cur); });
    });
    showCurrent();
}

void TransitionsPage::showCurrent() {
    QLayout *l = m_propsHost->layout();
    while (QLayoutItem *it = l->takeAt(0)) { delete it->widget(); delete it; }
    if (obs_source_t *t = Engine::instance().transition()) l->addWidget(new PropertiesView(t, m_propsHost));
}

// ===================================================================== Chat
ChatPage::ChatPage(bool compact, QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    if (compact) lay->setContentsMargins(4, 4, 4, 4);
    else { top->addWidget(heading(tr("Unified chat"))); top->addStretch(); }
    m_filterPlatform = new QComboBox; m_filterPlatform->addItem(tr("All platforms"), "all");
    m_filterPlatform->addItem("Twitch", "twitch"); m_filterPlatform->addItem("YouTube", "youtube");
    m_showHidden = new QCheckBox(tr("Show filtered"));
    m_fontSize = new QSpinBox; m_fontSize->setRange(8, 28); m_fontSize->setValue(Settings::instance().get("chat.fontPt", 11).toInt()); m_fontSize->setPrefix(tr("Text "));
    auto *mod = new QPushButton(tr("Moderation ▾"));
    auto *words = new QPushButton(tr("Blocked words…"));
    auto *connectBtn = accentButton(tr("Connect chat"), "mint");
    if (compact) {   // narrow toolbar so the chat panel can dock in tight spots
        m_fontSize->hide();
        m_filterPlatform->setItemText(0, tr("All"));
        m_filterPlatform->setMinimumContentsLength(6);
        m_showHidden->setText(tr("Filtered"));
        mod->setText(tr("Mod")); mod->setToolTip(tr("Moderation modes"));
        words->setText(tr("⛔")); words->setToolTip(tr("Blocked words"));
        connectBtn->setText(tr("⟳")); connectBtn->setToolTip(tr("Connect chat"));
        for (QWidget *w : {static_cast<QWidget *>(mod), static_cast<QWidget *>(words), static_cast<QWidget *>(connectBtn)}) w->setMaximumWidth(64);
        setMinimumWidth(240);
    }
    for (QWidget *w : {static_cast<QWidget *>(m_filterPlatform), static_cast<QWidget *>(m_showHidden), static_cast<QWidget *>(m_fontSize),
                       static_cast<QWidget *>(mod), static_cast<QWidget *>(words), static_cast<QWidget *>(connectBtn)}) top->addWidget(w);
    lay->addLayout(top);
    m_list = new QListWidget;
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setStyleSheet(QStringLiteral("font-size:%1pt;").arg(m_fontSize->value()));
    lay->addWidget(m_list, 1);
    auto *send = new QHBoxLayout;
    m_target = new QComboBox; m_target->addItem(tr("All connected"), "all"); m_target->addItem("Twitch", "twitch"); m_target->addItem("YouTube", "youtube");
    m_input = new QLineEdit; m_input->setPlaceholderText(tr("Say something cute…"));
    auto *sendBtn = accentButton(tr("Send ♡"));
    send->addWidget(m_target); send->addWidget(m_input, 1); send->addWidget(sendBtn);
    lay->addLayout(send);

    auto &hub = PlatformHub::instance();
    connect(&hub, &PlatformHub::chatMessage, this, [this](const ChatMessage &m) { ++SessionHistory::instance().chatMessages; append(m); });
    connect(&hub, &PlatformHub::chatMessageDeleted, this, [this](const QString &p, const QString &id) {
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->data(Qt::UserRole + 1).toString() == p && m_list->item(i)->data(Qt::UserRole + 2).toString() == id) {
                QFont f = m_list->item(i)->font(); f.setStrikeOut(true); m_list->item(i)->setFont(f);
            }
    });
    auto doSend = [this] { PlatformHub::instance().sendChat(m_target->currentData().toString(), m_input->text()); m_input->clear(); };
    connect(sendBtn, &QPushButton::clicked, this, doSend);
    connect(m_input, &QLineEdit::returnPressed, this, doSend);
    connect(connectBtn, &QPushButton::clicked, this, [] { PlatformHub::instance().startChat(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ChatPage::contextMenu);
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int v) { m_list->setStyleSheet(QStringLiteral("font-size:%1pt;").arg(v)); Settings::instance().set("chat.fontPt", v); });
    auto reloadAll = [this] { m_list->clear(); for (const auto &m : PlatformHub::instance().recentChat()) append(m); };
    connect(m_showHidden, &QCheckBox::toggled, this, reloadAll);
    connect(m_filterPlatform, &QComboBox::currentIndexChanged, this, reloadAll);
    connect(words, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(this, tr("Blocked words"), tr("One word or phrase per line. Matching messages are hidden locally (and from the chat-box widget)."),
                                                         PlatformHub::instance().blockedWords().join('\n'), &ok);
        if (ok) PlatformHub::instance().setBlockedWords(t.split('\n', Qt::SkipEmptyParts));
    });
    connect(mod, &QPushButton::clicked, this, [this, mod] {
        QMenu m(this);
        for (const QString p : {"twitch", "youtube"}) {
            PlatformAccount *a = PlatformHub::instance().account(p);
            QMenu *sub = m.addMenu(PlatformCatalog::get(p).name);
            const bool ready = a && a->isConnected();
            ChatMessage dummy; dummy.platform = p;
            auto addToggle = [&](ModAction act, const QString &label, int secs) {
                if (!a || !a->supports(act)) return;   // never advertise unsupported actions
                sub->addAction(label + tr(" on"), this, [act, dummy, secs] { PlatformHub::instance().moderate(act, dummy, secs, true); })->setEnabled(ready);
                sub->addAction(label + tr(" off"), this, [act, dummy] { PlatformHub::instance().moderate(act, dummy, 0, false); })->setEnabled(ready);
            };
            addToggle(ModAction::SlowMode, tr("Slow mode (30 s)"), 30);
            addToggle(ModAction::FollowerOnly, tr("Follower-only"), 600);
            addToggle(ModAction::SubscriberOnly, tr("Subscriber-only"), 0);
            if (sub->isEmpty()) sub->addAction(tr("Chat modes aren't available through this platform's API"))->setEnabled(false);
        }
        m.exec(mod->mapToGlobal(QPoint(0, mod->height())));
    });
    reloadAll();
}

void ChatPage::append(const ChatMessage &m) {
    if (m.hidden && !m_showHidden->isChecked()) return;
    const QString pf = m_filterPlatform->currentData().toString();
    if (pf != "all" && pf != m.platform) return;
    const QString badges = m.badges.contains("broadcaster") ? "👑 " : m.badges.contains("moderator") ? "🛡 " : m.badges.contains("subscriber") || m.badges.contains("member") ? "♡ " : "";
    auto *it = new QListWidgetItem(QStringLiteral("[%1] %2%3: %4%5").arg(m.ts.toString("HH:mm"), badges, m.user, m.text,
                                   m.hidden ? tr("   (filtered: %1)").arg(m.hiddenReason) : QString()));
    it->setForeground(m.hidden ? QColor(170, 160, 170) : theme::Charcoal);
    it->setData(Qt::UserRole, QVariant::fromValue(m.userId));
    it->setData(Qt::UserRole + 1, m.platform);
    it->setData(Qt::UserRole + 2, m.id);
    it->setData(Qt::UserRole + 3, m.user);
    it->setIcon([&] { QPixmap pm(12, 12); pm.fill(Qt::transparent); QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor(PlatformCatalog::get(m.platform).color)); p.setPen(Qt::NoPen); p.drawEllipse(1, 1, 10, 10); return QIcon(pm); }());
    if (m.deleted) { QFont f = it->font(); f.setStrikeOut(true); it->setFont(f); }
    QScrollBar *sb = m_list->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;   // don't yank the view while the user scrolls back
    m_list->addItem(it);
    while (m_list->count() > 400) delete m_list->takeItem(0);
    if (atBottom) m_list->scrollToBottom();
}

void ChatPage::contextMenu(const QPoint &pt) {
    auto *it = m_list->itemAt(pt);
    if (!it) return;
    ChatMessage m;
    m.userId = it->data(Qt::UserRole).toString(); m.platform = it->data(Qt::UserRole + 1).toString();
    m.id = it->data(Qt::UserRole + 2).toString(); m.user = it->data(Qt::UserRole + 3).toString();
    PlatformAccount *a = PlatformHub::instance().account(m.platform);
    QMenu menu(this);
    auto add = [&](ModAction act, const QString &label, int secs) {
        if (a && a->supports(act)) menu.addAction(label, this, [m, act, secs] { PlatformHub::instance().moderate(act, m, secs); });
    };
    add(ModAction::Delete, tr("Delete message"), 0);
    add(ModAction::Timeout, tr("Timeout 10 minutes"), 600);
    add(ModAction::Ban, tr("Ban %1").arg(m.user), 0);
    add(ModAction::Block, tr("Block %1").arg(m.user), 0);
    menu.addAction(tr("Copy message"), this, [it] { QGuiApplication::clipboard()->setText(it->text()); });
    menu.exec(m_list->viewport()->mapToGlobal(pt));
}

// ===================================================================== Activity
ActivityPage::ActivityPage(bool compact, QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    if (compact) lay->setContentsMargins(4, 4, 4, 4);
    else top->addWidget(heading(tr("Activity feed")));
    top->addStretch();
    auto *test = new QPushButton(tr("Send test event ▾"));
    top->addWidget(test);
    lay->addLayout(top);
    m_list = new QListWidget;
    lay->addWidget(m_list, 1);
    auto add = [this](const StreamEvent &e) {
        static const QHash<QString, QString> icons = {{"follow", "♡"}, {"subscribe", "★"}, {"gift", "🎁"}, {"raid", "🎉"},
            {"cheer", "✦"}, {"donation", "💗"}, {"membership", "★"}, {"redemption", "🎀"}};
        auto *it = new QListWidgetItem(QStringLiteral("%1  %2  %3 — %4%5%6").arg(e.ts.toString("HH:mm:ss"), icons.value(e.type, "•"), e.user, e.type,
            e.amount > 0 ? QStringLiteral(" ×%1").arg(e.amount) : QString(), e.message.isEmpty() ? QString() : " · " + e.message)
            + (e.test ? tr("  (test)") : QString()));
        it->setToolTip(PlatformCatalog::get(e.platform).name);
        m_list->insertItem(0, it);
    };
    for (const auto &e : PlatformHub::instance().recentEvents()) add(e);
    connect(&PlatformHub::instance(), &PlatformHub::streamEvent, this, add);
    connect(test, &QPushButton::clicked, this, [this, test] {
        QMenu m(this);
        for (const QString &t : WidgetManager::alertTypes()) m.addAction(t, this, [t] { PlatformHub::instance().emitTestEvent(t); });
        m.exec(test->mapToGlobal(QPoint(0, test->height())));
    });
}

// ===================================================================== Analytics
AnalyticsPage::AnalyticsPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(heading(tr("Session analytics")));
    auto *grid = new QGridLayout;
    const QList<QPair<QString, QString>> items = {{"live", tr("Live for")}, {"peak", tr("Peak viewers")}, {"follows", tr("New followers")},
        {"subs", tr("New subs + gifts")}, {"cheers", tr("Bits / tips")}, {"chat", tr("Chat messages")},
        {"avgkbps", tr("Average upload")}, {"avgcpu", tr("Average CPU")}, {"lag", tr("Render lag")}};
    for (int i = 0; i < items.size(); ++i) {
        auto *box = new QFrame; box->setObjectName("GlassPanel");
        auto *v = new QVBoxLayout(box);
        auto *c = new QLabel(items[i].second); c->setObjectName("Muted");
        auto *val = new QLabel("—"); val->setObjectName("H2");
        v->addWidget(c); v->addWidget(val);
        m_stats.insert(items[i].first, val);
        grid->addWidget(box, i / 3, i % 3);
    }
    lay->addLayout(grid);
    m_chart = new SparkChart;
    lay->addWidget(m_chart);
    auto *note = new QLabel(tr("These numbers are measured locally by HeartCast during this session. Platform-side analytics "
                               "(watch time, demographics) live in Twitch Creator Dashboard / YouTube Studio."));
    note->setWordWrap(true); note->setObjectName("Muted");
    lay->addWidget(note);
    lay->addStretch();
}

void AnalyticsPage::refresh() {
    auto &h = SessionHistory::instance();
    auto &hub = PlatformHub::instance();
    auto avg = [](const QList<double> &l) { double s = 0; for (double v : l) s += v; return l.isEmpty() ? 0.0 : s / l.size(); };
    m_stats["live"]->setText(h.liveStartMs ? fmtDur(QDateTime::currentMSecsSinceEpoch() - h.liveStartMs) : tr("offline"));
    m_stats["peak"]->setText(h.peakViewers > 0 ? QString::number(h.peakViewers) : "—");
    m_stats["follows"]->setText(QString::number(hub.sessionCount("follow")));
    m_stats["subs"]->setText(QString::number(hub.sessionCount("subscribe") + hub.sessionCount("gift")));
    m_stats["cheers"]->setText(QString::number(hub.sessionAmount("cheer") + hub.sessionAmount("donation")));
    m_stats["chat"]->setText(QString::number(h.chatMessages));
    m_stats["avgkbps"]->setText(QStringLiteral("%1 kbps").arg(int(avg(h.kbps))));
    m_stats["avgcpu"]->setText(QStringLiteral("%1%").arg(avg(h.cpu), 0, 'f', 1));
    const auto st = Engine::instance().stats();
    m_stats["lag"]->setText(st.renderTotal ? QStringLiteral("%1 (%2%)").arg(st.renderLagged).arg(100.0 * st.renderLagged / st.renderTotal, 0, 'f', 2) : "0");
    m_chart->update();
}

// ===================================================================== Plugins
PluginsPage::PluginsPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Plugins")));
    top->addStretch();
    auto *open = new QPushButton(tr("Open plugins folder"));
    auto *rescan = new QPushButton(tr("Rescan"));
    top->addWidget(open); top->addWidget(rescan);
    lay->addLayout(top);
    auto *warn = new QLabel(tr("Plugins are native code that runs inside HeartCast. Only enable plugins from people you trust. "
                               "Permissions limit what a plugin can ask HeartCast to do, but they are not a security sandbox."));
    warn->setWordWrap(true); warn->setProperty("pill", "warn");
    lay->addWidget(warn);
    m_table = new QTableWidget(0, 7);
    m_table->setHorizontalHeaderLabels({tr("Enabled"), tr("Name"), tr("Version"), tr("Kind"), tr("Permissions"), tr("Signature"), tr("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    lay->addWidget(m_table, 2);
    lay->addWidget(heading(tr("Loaded engine modules (sources, filters, encoders, outputs)"), "H2"));
    m_modules = new QListWidget;
    lay->addWidget(m_modules, 1);
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::plugins())); });
    connect(rescan, &QPushButton::clicked, this, &PluginsPage::reload);
    connect(m_table, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (c != 0) return;
        const QString id = m_table->item(r, 1)->data(Qt::UserRole).toString();
        const bool on = m_table->item(r, 0)->checkState() == Qt::Checked;
        if (on) {
            const QString perms = m_table->item(r, 4)->text();
            if (QMessageBox::question(this, tr("Enable plugin"), tr("Enable \"%1\"?\n\nIt requests: %2\nSignature: %3")
                    .arg(m_table->item(r, 1)->text(), perms.isEmpty() ? tr("no special permissions") : perms, m_table->item(r, 5)->text())) != QMessageBox::Yes) {
                reload(); return;
            }
        }
        QString err;
        if (!PluginManager::instance().setEnabled(id, on, &err) || !err.isEmpty())
            QMessageBox::information(this, tr("Plugins"), err.isEmpty() ? tr("Could not change plugin state.") : err);
        reload();
    });
    reload();
}

void PluginsPage::reload() {
    QSignalBlocker b(m_table);
    const auto entries = PluginManager::instance().scan();
    m_table->setRowCount(int(entries.size()));
    for (int r = 0; r < entries.size(); ++r) {
        const auto &e = entries[r];
        auto *on = new QTableWidgetItem; on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        on->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, 0, on);
        auto *n = new QTableWidgetItem(e.name); n->setData(Qt::UserRole, e.id); n->setToolTip(e.description);
        m_table->setItem(r, 1, n);
        m_table->setItem(r, 2, new QTableWidgetItem(e.version));
        m_table->setItem(r, 3, new QTableWidgetItem(e.kind));
        m_table->setItem(r, 4, new QTableWidgetItem(e.permissions.join(", ")));
        m_table->setItem(r, 5, new QTableWidgetItem(e.signer.isEmpty() ? tr("unsigned") : tr("signed: %1").arg(e.signer)));
        m_table->setItem(r, 6, new QTableWidgetItem(!e.error.isEmpty() ? e.error : e.loaded ? tr("loaded ✓") : e.enabled ? tr("enabled") : tr("disabled")));
    }
    m_modules->clear();
    obs_enum_modules([](void *p, obs_module_t *m) {
        const char *name = obs_get_module_name(m);
        const char *file = obs_get_module_file_name(m);
        static_cast<QListWidget *>(p)->addItem(QString::fromUtf8(name ? name : file ? file : "?"));
    }, m_modules);
    if (m_modules->count() == 0) m_modules->addItem(tr("(engine not running)"));
}

} // namespace bc
