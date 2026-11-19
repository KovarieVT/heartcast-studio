#include "ui/StudioDocks.h"
#include "app/Theme.h"
#include "core/Paths.h"
#include "engine/Engine.h"
#include "ui/PreviewWidget.h"
#include "ui/PropertiesView.h"
#include "platforms/PlatformHub.h"
#include <QTableWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace bc {

static QToolButton *tinyButton(const QString &text, const QString &tip, QWidget *parent) {
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(false);
    b->setMinimumSize(30, 28);
    return b;
}

// ===================================================================== Scenes
ScenesPanel::ScenesPanel(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    m_list = new QListWidget;
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    lay->addWidget(m_list, 1);
    auto *row = new QHBoxLayout;
    auto *add = tinyButton("＋", tr("Add scene"), this);
    auto *del = tinyButton("－", tr("Remove scene"), this);
    auto *dup = tinyButton("⧉", tr("Duplicate scene"), this);
    auto *up = tinyButton("▲", tr("Move up"), this);
    auto *down = tinyButton("▼", tr("Move down"), this);
    for (auto *b : {add, del, dup, up, down}) row->addWidget(b);
    row->addStretch();
    lay->addLayout(row);

    auto &eng = Engine::instance();
    connect(add, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("New scene"), tr("Scene name:"), QLineEdit::Normal, tr("Scene"), &ok);
        if (ok) Engine::instance().createScene(n);
    });
    connect(del, &QToolButton::clicked, this, [this] {
        if (!m_list->currentItem()) return;
        const QString n = m_list->currentItem()->text();
        if (QMessageBox::question(this, tr("Remove scene"), tr("Remove \"%1\"?").arg(n)) == QMessageBox::Yes)
            if (!Engine::instance().removeScene(n)) QMessageBox::information(this, tr("Remove scene"), tr("You need at least one scene ♡"));
    });
    connect(dup, &QToolButton::clicked, this, [this] { if (m_list->currentItem()) Engine::instance().duplicateScene(m_list->currentItem()->text()); });
    connect(up, &QToolButton::clicked, this, [this] { if (auto *it = m_list->currentItem()) Engine::instance().moveScene(it->text(), m_list->row(it) - 1); });
    connect(down, &QToolButton::clicked, this, [this] { if (auto *it = m_list->currentItem()) Engine::instance().moveScene(it->text(), m_list->row(it) + 1); });
    connect(m_list, &QListWidget::currentTextChanged, this, [this](const QString &n) { if (!m_reloading && !n.isEmpty()) Q_EMIT sceneActivated(n); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ScenesPanel::contextMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (m_reloading) return;
        for (int i = 0; i < m_list->count(); ++i) Engine::instance().moveScene(m_list->item(i)->text(), i);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        if (m_reloading) return;
        const QString old = it->data(Qt::UserRole).toString();
        if (old != it->text() && !Engine::instance().renameScene(old, it->text())) {
            QMessageBox::warning(this, tr("Rename"), tr("That name is empty or already used."));
            reload();
        }
    });
    connect(&eng, &Engine::scenesChanged, this, &ScenesPanel::reload);
    connect(&eng, &Engine::previewSceneChanged, this, &ScenesPanel::reload);
    connect(&eng, &Engine::programSceneChanged, this, &ScenesPanel::reload);
    reload();
}

void ScenesPanel::reload() {
    m_reloading = true;
    auto &eng = Engine::instance();
    m_list->clear();
    for (const QString &n : eng.sceneOrder()) {
        auto *it = new QListWidgetItem((n == eng.programScene() ? QStringLiteral("● ") : QString()) + n);
        it->setText(n);
        it->setData(Qt::UserRole, n);
        it->setFlags(it->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        if (n == eng.programScene()) { QFont f = it->font(); f.setBold(true); it->setFont(f); it->setForeground(theme::DeepRose); }
        m_list->addItem(it);
        if (n == eng.previewScene()) m_list->setCurrentItem(it);
    }
    m_reloading = false;
}

void ScenesPanel::contextMenu(const QPoint &pt) {
    QListWidgetItem *it = m_list->itemAt(pt);
    QMenu m(this);
    m.addAction(tr("Add scene"), this, [this] { Engine::instance().createScene(tr("Scene")); });
    if (it) {
        const QString n = it->text();
        m.addAction(tr("Rename"), this, [this, it] { m_list->editItem(it); });
        m.addAction(tr("Duplicate"), this, [n] { Engine::instance().duplicateScene(n); });
        m.addAction(tr("Send to program now"), this, [n] { Engine::instance().transitionTo(n); });
        m.addSeparator();
        m.addAction(tr("Remove"), this, [this, n] { if (!Engine::instance().removeScene(n)) QMessageBox::information(this, tr("Remove"), tr("You need at least one scene ♡")); });
    }
    m.exec(m_list->mapToGlobal(pt));
}

// ===================================================================== Sources
SourcesPanel::SourcesPanel(PreviewWidget *preview, QWidget *parent) : QWidget(parent), m_preview(preview) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    auto *add = tinyButton("＋", tr("Add source"), this);
    auto *del = tinyButton("－", tr("Remove"), this);
    auto *props = tinyButton("⚙", tr("Properties"), this);
    auto *filt = tinyButton("✦", tr("Filters & effects"), this);
    auto *up = tinyButton("▲", tr("Move up"), this);
    auto *down = tinyButton("▼", tr("Move down"), this);
    for (auto *b : {add, del, props, filt, up, down}) row->addWidget(b);
    row->addStretch();
    lay->addLayout(row);

    connect(add, &QToolButton::clicked, this, [this, add] { addSourceMenu(add); });
    connect(del, &QToolButton::clicked, this, &SourcesPanel::removeSelected);
    connect(props, &QToolButton::clicked, this, [this] { if (auto *i = currentItem()) (new PropertiesDialog(obs_sceneitem_get_source(i), this))->show(); });
    connect(filt, &QToolButton::clicked, this, [this] { if (auto *i = currentItem()) (new FiltersDialog(obs_sceneitem_get_source(i), this))->show(); });
    connect(up, &QToolButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_UP); });
    connect(down, &QToolButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_DOWN); });
    connect(m_tree, &QTreeWidget::itemChanged, this, &SourcesPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SourcesPanel::contextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] { if (auto *i = currentItem()) (new PropertiesDialog(obs_sceneitem_get_source(i), this))->show(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { if (!m_reloading) m_preview->selectItem(currentItem()); });
    connect(m_preview, &PreviewWidget::itemSelected, this, [this](obs_sceneitem_t *item) {
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toLongLong() == (item ? obs_sceneitem_get_id(item) : -1)) {
                QSignalBlocker b(m_tree); m_tree->setCurrentItem(*it); return;
            }
    });
    connect(&Engine::instance(), &Engine::sceneItemsChanged, this, [this](const QString &s) { if (s == m_preview->sceneName()) reload(); });
    connect(&Engine::instance(), &Engine::previewSceneChanged, this, &SourcesPanel::reload);
}

obs_sceneitem_t *SourcesPanel::currentItem() const {
    QTreeWidgetItem *it = m_tree->currentItem();
    obs_scene_t *sc = Engine::instance().scene(m_preview->sceneName());
    if (!it || !sc) return nullptr;
    const int64_t id = it->data(0, Qt::UserRole).toLongLong();
    obs_sceneitem_t *found = obs_scene_find_sceneitem_by_id(sc, id);
    if (!found) {   // may be inside a group
        struct C { int64_t id; obs_sceneitem_t *f = nullptr; } c{id};
        obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *i, void *p) {
            auto *cc = static_cast<C *>(p);
            if (obs_sceneitem_is_group(i))
                obs_sceneitem_group_enum_items(i, [](obs_scene_t *, obs_sceneitem_t *g, void *pp) {
                    auto *c2 = static_cast<C *>(pp);
                    if (obs_sceneitem_get_id(g) == c2->id) c2->f = g;
                    return true;
                }, cc);
            return true;
        }, &c);
        found = c.f;
    }
    return found;
}

static void addItemRow(QTreeWidget *tree, QTreeWidgetItem *parent, obs_sceneitem_t *item) {
    obs_source_t *src = obs_sceneitem_get_source(item);
    auto *row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem;
    row->setText(0, QString::fromUtf8(obs_source_get_name(src)));
    row->setData(0, Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
    row->setToolTip(0, QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(src))));
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    row->setText(1, QObject::tr("👁"));
    row->setCheckState(1, obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
    row->setText(2, QObject::tr("🔒"));
    row->setCheckState(2, obs_sceneitem_locked(item) ? Qt::Checked : Qt::Unchecked);
    if (!parent) tree->insertTopLevelItem(0, row);   // libobs enumerates bottom→top
    else parent->insertChild(0, row);
    if (obs_sceneitem_is_group(item)) {
        row->setExpanded(true);
        obs_sceneitem_group_enum_items(item, [](obs_scene_t *, obs_sceneitem_t *child, void *p) {
            auto *r = static_cast<QTreeWidgetItem *>(p);
            addItemRow(r->treeWidget(), r, child);
            return true;
        }, row);
    }
}

void SourcesPanel::reload() {
    m_reloading = true;
    const qlonglong cur = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toLongLong() : -1;
    m_tree->clear();
    if (obs_scene_t *sc = Engine::instance().scene(m_preview->sceneName())) {
        obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *item, void *p) {
            addItemRow(static_cast<QTreeWidget *>(p), nullptr, item);
            return true;
        }, m_tree);
    }
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toLongLong() == cur) m_tree->setCurrentItem(*it);
    m_tree->expandAll();
    m_reloading = false;
}

void SourcesPanel::onItemChanged(QTreeWidgetItem *it, int col) {
    if (m_reloading) return;
    m_tree->setCurrentItem(it);
    obs_sceneitem_t *item = currentItem();
    if (!item) return;
    if (col == 1) obs_sceneitem_set_visible(item, it->checkState(1) == Qt::Checked);
    if (col == 2) obs_sceneitem_set_locked(item, it->checkState(2) == Qt::Checked);
    if (col == 0) {
        const QString n = it->text(0).trimmed();
        OBSSourceAutoRelease ex = obs_get_source_by_name(n.toUtf8().constData());
        obs_source_t *src = obs_sceneitem_get_source(item);
        if (!n.isEmpty() && !ex) obs_source_set_name(src, n.toUtf8().constData());
        else { QSignalBlocker b(m_tree); it->setText(0, QString::fromUtf8(obs_source_get_name(src))); }
    }
}

static const QList<QPair<QString, QString>> &widgetKinds() {
    static const QList<QPair<QString, QString>> k = {
        {"alerts", QObject::tr("Alert box")}, {"chat", QObject::tr("Chat box")}, {"events", QObject::tr("Event list")},
        {"label", QObject::tr("Stream label")}, {"goal", QObject::tr("Goal bar")}, {"countdown", QObject::tr("Countdown")},
        {"stopwatch", QObject::tr("Stopwatch")}, {"social", QObject::tr("Social panel")}, {"sponsor", QObject::tr("Sponsor panel")},
        {"viewers", QObject::tr("Viewer count")}, {"text_panel", QObject::tr("Text panel")}};
    return k;
}

void SourcesPanel::addWidgetOfKind(const QString &kind, const QString &pack) {
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "kind", kind.toUtf8().constData());
    obs_data_set_string(st, "pack", pack.toUtf8().constData());
    QSize sz(800, 240);
    if (kind == "chat") sz = {520, 680};
    else if (kind == "events") sz = {480, 420};
    else if (kind == "goal" || kind == "label" || kind == "viewers") sz = {720, 140};
    else if (kind == "social" || kind == "sponsor") sz = {520, 260};
    obs_data_set_int(st, "width", sz.width());
    obs_data_set_int(st, "height", sz.height());
    QString name;
    for (const auto &k : widgetKinds()) if (k.first == kind) name = k.second;
    if (obs_sceneitem_t *item = Engine::instance().addSource(m_preview->sceneName(), "bc_widget", name + " ♡", st)) {
        obs_video_info ovi{}; obs_get_video_info(&ovi);
        vec2 pos{float(ovi.base_width - sz.width()) / 2, float(ovi.base_height - sz.height()) / 2};
        if (kind == "chat") pos = {float(ovi.base_width - sz.width() - 30), 30};
        obs_sceneitem_set_pos(item, &pos);
        m_preview->selectItem(item);
    }
}

void SourcesPanel::addSourceMenu(QWidget *anchor) {
    auto &eng = Engine::instance();
    QMenu menu(this);
    const QHash<QString, QString> catNames = {{"capture", tr("Capture ✦")}, {"audio", tr("Audio ♪")},
                                              {"media", tr("Media && images")}, {"creative", tr("Text, colour && creative")}};
    QHash<QString, QMenu *> subs;
    for (const QString &c : {"capture", "audio", "media", "creative"}) subs[c] = menu.addMenu(catNames[c]);
    for (const auto &t : eng.sourceTypes()) {
        QMenu *m = subs.value(t.category, subs["creative"]);
        m->addAction(t.name, this, [this, t] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Add %1").arg(t.name), tr("Name:"), QLineEdit::Normal,
                                                       Engine::instance().uniqueSourceName(t.name), &ok);
            if (!ok) return;
            obs_sceneitem_t *item = Engine::instance().addSource(m_preview->sceneName(), t.id, name);
            if (!item) return;
            if (t.video && (t.category == "capture")) Engine::instance().fitItemToCanvas(item);
            m_preview->selectItem(item);
            (new PropertiesDialog(obs_sceneitem_get_source(item), this))->show();
        });
    }
    QMenu *w = menu.addMenu(tr("Widgets ♡"));
    for (const auto &k : widgetKinds()) w->addAction(k.second, this, [this, id = k.first] { addWidgetOfKind(id); });
    menu.addSeparator();
    QMenu *existing = menu.addMenu(tr("Add existing source"));
    obs_enum_sources([](void *p, obs_source_t *s) {
        auto *m = static_cast<QMenu *>(p);
        OBSSource keep(s);
        m->addAction(QString::fromUtf8(obs_source_get_name(s)), m, [keep] {
            Engine &e = Engine::instance();
            const QString scene = e.studioMode() ? e.previewScene() : e.programScene();
            e.addExistingSource(scene, keep);
        });
        return true;
    }, existing);
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

void SourcesPanel::removeSelected() {
    QList<obs_sceneitem_t *> items;
    for (auto *it : m_tree->selectedItems()) { m_tree->setCurrentItem(it); if (auto *i = currentItem()) items << i; }
    if (items.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remove"), tr("Remove %n source(s) from this scene?", nullptr, int(items.size()))) != QMessageBox::Yes) return;
    m_preview->selectItem(nullptr);
    for (auto *i : items) obs_sceneitem_remove(i);
    Q_EMIT Engine::instance().sceneItemsChanged(m_preview->sceneName());
}

void SourcesPanel::moveSelected(obs_order_movement mv) {
    if (auto *i = currentItem()) { obs_sceneitem_set_order(i, mv); reload(); }
}

void SourcesPanel::duplicateSelected() {
    obs_sceneitem_t *i = currentItem();
    if (!i) return;
    obs_source_t *src = obs_sceneitem_get_source(i);
    const QString n = Engine::instance().uniqueSourceName(QString::fromUtf8(obs_source_get_name(src)));
    OBSSourceAutoRelease dup = obs_source_duplicate(src, n.toUtf8().constData(), false);
    if (!dup) return;
    if (obs_sceneitem_t *ni = Engine::instance().addExistingSource(m_preview->sceneName(), dup)) {
        ItemTransform::read(i).apply(ni);
        m_preview->selectItem(ni);
    }
}

void SourcesPanel::groupSelected() {
    QList<obs_sceneitem_t *> items;
    for (auto *it : m_tree->selectedItems()) { m_tree->setCurrentItem(it); if (auto *i = currentItem()) items << i; }
    obs_scene_t *sc = Engine::instance().scene(m_preview->sceneName());
    if (items.isEmpty() || !sc) return;
    const QString n = Engine::instance().uniqueSourceName(tr("Group"));
    obs_sceneitem_t *g = obs_scene_add_group2(sc, n.toUtf8().constData(), true);
    for (auto *i : items) obs_sceneitem_group_add_item(g, i);
    Q_EMIT Engine::instance().sceneItemsChanged(m_preview->sceneName());
}

void SourcesPanel::contextMenu(const QPoint &pt) {
    QMenu m(this);
    m.addAction(tr("Add source…"), this, [this] { addSourceMenu(m_tree); });
    if (obs_sceneitem_t *i = currentItem()) {
        obs_source_t *src = obs_sceneitem_get_source(i);
        m.addAction(tr("Properties…"), this, [this, src] { (new PropertiesDialog(src, this))->show(); });
        m.addAction(tr("Filters && effects…"), this, [this, src] { (new FiltersDialog(src, this))->show(); });
        m.addAction(tr("Edit transform…"), this, [this, i] { (new TransformDialog(i, m_preview, this))->show(); });
        QMenu *t = m.addMenu(tr("Transform"));
        t->addAction(tr("Fit to screen"), this, [this, i] { const auto b = ItemTransform::read(i); Engine::instance().fitItemToCanvas(i); m_preview->pushTransformUndo(i, b, tr("Fit to screen")); });
        t->addAction(tr("Reset transform"), this, [this, i] {
            const auto b = ItemTransform::read(i);
            ItemTransform r; r.scale = {1, 1}; r.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP; r.apply(i);
            m_preview->pushTransformUndo(i, b, tr("Reset transform"));
        });
        t->addAction(tr("Center"), this, [this, i] {
            const auto b = ItemTransform::read(i);
            obs_video_info ovi{}; obs_get_video_info(&ovi);
            obs_sceneitem_set_alignment(i, OBS_ALIGN_CENTER);
            vec2 p{ovi.base_width / 2.f, ovi.base_height / 2.f}; obs_sceneitem_set_pos(i, &p);
            m_preview->pushTransformUndo(i, b, tr("Center"));
        });
        m.addSeparator();
        m.addAction(tr("Duplicate"), this, &SourcesPanel::duplicateSelected);
        m.addAction(tr("Group selected"), this, &SourcesPanel::groupSelected);
        if (obs_sceneitem_is_group(i)) m.addAction(tr("Ungroup"), this, [this, i] { obs_sceneitem_group_ungroup(i); reload(); });
        QMenu *order = m.addMenu(tr("Order"));
        order->addAction(tr("Bring to front"), this, [this] { moveSelected(OBS_ORDER_MOVE_TOP); });
        order->addAction(tr("Send to back"), this, [this] { moveSelected(OBS_ORDER_MOVE_BOTTOM); });
        m.addAction(tr("Rename"), this, [this] { if (auto *it = m_tree->currentItem()) m_tree->editItem(it, 0); });
        m.addSeparator();
        m.addAction(tr("Remove"), this, &SourcesPanel::removeSelected);
    }
    m.exec(m_tree->viewport()->mapToGlobal(pt));
}

// ===================================================================== Audio mixer
LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent) { setMinimumHeight(12); setMaximumHeight(14); }

void LevelMeter::setLevels(float mag, float peak) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_mag = std::isfinite(mag) ? mag : -60.f;
    m_peak = std::isfinite(peak) ? peak : -60.f;
    if (m_peak >= m_hold || now - m_holdMs > 1500) { m_hold = m_peak; m_holdMs = now; }
    if (m_peak > -0.5f) m_clipMs = now;
    update();
}

void LevelMeter::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(0, 1, -16, -1);
    auto x = [&](float db) { return r.left() + r.width() * qBound(0.0, (db + 60.0) / 60.0, 1.0); };
    p.setPen(Qt::NoPen); p.setBrush(QColor(237, 228, 255)); p.drawRoundedRect(r, 5, 5);
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0, theme::PastelMint); g.setColorAt(0.7, theme::CottonCandy); g.setColorAt(0.92, theme::BlushPink); g.setColorAt(1, QColor("#E0457B"));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(r.left(), r.top(), x(m_peak) - r.left(), r.height()), 5, 5);
    p.setBrush(QColor(255, 255, 255, 120));
    p.drawRect(QRectF(r.left(), r.top(), x(m_mag) - r.left(), r.height() / 3));
    p.setBrush(theme::DeepRose);
    p.drawRect(QRectF(x(m_hold) - 1, r.top(), 2, r.height()));
    const bool clip = QDateTime::currentMSecsSinceEpoch() - m_clipMs < 1000;
    p.setBrush(clip ? QColor("#E0457B") : QColor(220, 210, 220));
    p.drawEllipse(QPointF(rect().right() - 7, rect().center().y()), 5, 5);
}

AudioStrip::AudioStrip(obs_source_t *src, QWidget *parent) : QWidget(parent), m_src(src) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(3);
    auto *top = new QHBoxLayout;
    auto *name = new QLabel(QString::fromUtf8(obs_source_get_name(src)));
    name->setStyleSheet("font-weight:700;");
    m_db = new QLabel("-inf dB"); m_db->setObjectName("Muted");
    m_mute = tinyButton("🔊", tr("Mute"), this); m_mute->setCheckable(true);
    m_solo = tinyButton("S", tr("Solo (mute every other source)"), this); m_solo->setCheckable(true);
    m_monitor = tinyButton("🎧", tr("Monitor (hear this source in your headphones)"), this); m_monitor->setCheckable(true);
    auto *gear = tinyButton("⚙", tr("Filters & advanced"), this);
    top->addWidget(name, 1); top->addWidget(m_db); top->addWidget(m_mute); top->addWidget(m_solo); top->addWidget(m_monitor); top->addWidget(gear);
    lay->addLayout(top);
    m_levels = new LevelMeter(this);
    lay->addWidget(m_levels);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 1000);
    lay->addWidget(m_slider);

    m_fader = obs_fader_create(OBS_FADER_LOG);
    obs_fader_attach_source(m_fader, src);
    m_meter = obs_volmeter_create(OBS_FADER_LOG);
    obs_volmeter_attach_source(m_meter, src);
    obs_volmeter_add_callback(m_meter, volmeterCb, this);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        obs_fader_set_deflection(m_fader, float(v) / 1000.f);
        const float db = obs_fader_get_db(m_fader);
        m_db->setText(std::isfinite(db) ? QString::number(db, 'f', 1) + " dB" : "-inf dB");
    });
    connect(m_mute, &QToolButton::toggled, this, [this](bool on) { m_userMuted = on; obs_source_set_muted(m_src, on || m_soloMuted); m_mute->setText(on ? "🔇" : "🔊"); });
    connect(m_solo, &QToolButton::toggled, this, [this](bool on) { Q_EMIT soloToggled(this, on); });
    connect(m_monitor, &QToolButton::toggled, this, [this](bool on) {
        obs_source_set_monitoring_type(m_src, on ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT : OBS_MONITORING_TYPE_NONE);
    });
    connect(gear, &QToolButton::clicked, this, [this, gear] {
        QMenu m(this);
        m.addAction(tr("Filters (noise suppression, compressor, EQ…)"), this, [this] { (new FiltersDialog(m_src, this))->show(); });
        m.addAction(tr("Advanced (sync offset, balance, tracks)…"), this, [this] { openAudioAdvanced(m_src, this); });
        m.addAction(tr("Properties…"), this, [this] { (new PropertiesDialog(m_src, this))->show(); });
        m.exec(gear->mapToGlobal(QPoint(0, gear->height())));
    });
    // Keep UI in sync if mute changes elsewhere (hotkey, plugin…)
    m_sigs.emplace_back(obs_source_get_signal_handler(src), "mute", [](void *d, calldata_t *cd) {
        auto *self = static_cast<AudioStrip *>(d);
        const bool muted = calldata_bool(cd, "muted");
        QMetaObject::invokeMethod(self, [self, muted] { QSignalBlocker b(self->m_mute); if (!self->m_soloMuted) { self->m_mute->setChecked(muted); self->m_mute->setText(muted ? "🔇" : "🔊"); } }, Qt::QueuedConnection);
    }, this);
    syncFromSource();
}

AudioStrip::~AudioStrip() {
    m_sigs.clear();
    obs_volmeter_remove_callback(m_meter, volmeterCb, this);
    obs_volmeter_destroy(m_meter);
    obs_fader_destroy(m_fader);
    if (m_soloMuted) obs_source_set_muted(m_src, m_userMuted);
}

void AudioStrip::syncFromSource() {
    QSignalBlocker b1(m_slider), b2(m_mute), b3(m_monitor);
    m_slider->setValue(int(obs_fader_get_deflection(m_fader) * 1000));
    const float db = obs_fader_get_db(m_fader);
    m_db->setText(std::isfinite(db) ? QString::number(db, 'f', 1) + " dB" : "-inf dB");
    m_userMuted = obs_source_muted(m_src);
    m_mute->setChecked(m_userMuted);
    m_mute->setText(m_userMuted ? "🔇" : "🔊");
    m_monitor->setChecked(obs_source_get_monitoring_type(m_src) != OBS_MONITORING_TYPE_NONE);
}

void AudioStrip::setSoloMuted(bool m) {
    m_soloMuted = m;
    obs_source_set_muted(m_src, m || m_userMuted);
    setEnabled(!m || m_solo->isChecked());
}

void AudioStrip::volmeterCb(void *data, const float mag[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                            const float[MAX_AUDIO_CHANNELS]) {
    // audio thread: just store the loudest channel; the UI polls at 30 fps.
    auto *self = static_cast<AudioStrip *>(data);
    self->m_mag.store(qMax(mag[0], mag[1]));
    self->m_peak.store(qMax(peak[0], peak[1]));
}

void AudioStrip::pollMeter() { m_levels->setLevels(m_mag.load(), m_peak.load()); }

MixerPanel::MixerPanel(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget;
    m_lay = new QVBoxLayout(host);
    m_lay->addStretch();
    scroll->setWidget(host);
    outer->addWidget(scroll);
    m_poll = new QTimer(this);
    connect(m_poll, &QTimer::timeout, this, [this] { for (auto *s : std::as_const(m_strips)) s->pollMeter(); });
    m_poll->start(33);
    auto &e = Engine::instance();
    connect(&e, &Engine::sceneItemsChanged, this, &MixerPanel::rebuild);
    connect(&e, &Engine::programSceneChanged, this, &MixerPanel::rebuild);
    connect(&e, &Engine::previewSceneChanged, this, &MixerPanel::rebuild);
    rebuild();
}

void MixerPanel::rebuild() {
    QList<obs_source_t *> sources;
    auto &e = Engine::instance();
    if (e.desktopAudio()) sources << e.desktopAudio();
    if (e.micAudio()) sources << e.micAudio();
    for (const QString &sn : {e.programScene(), e.previewScene()}) {
        obs_scene_t *sc = e.scene(sn);
        if (!sc) continue;
        obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *item, void *p) {
            auto *l = static_cast<QList<obs_source_t *> *>(p);
            auto visit = [l](obs_sceneitem_t *it) {
                obs_source_t *s = obs_sceneitem_get_source(it);
                if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) && !l->contains(s)) l->append(s);
            };
            visit(item);
            if (obs_sceneitem_is_group(item))
                obs_sceneitem_group_enum_items(item, [](obs_scene_t *, obs_sceneitem_t *c, void *pp) {
                    auto *ll = static_cast<QList<obs_source_t *> *>(pp);
                    obs_source_t *s = obs_sceneitem_get_source(c);
                    if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) && !ll->contains(s)) ll->append(s);
                    return true;
                }, l);
            return true;
        }, &sources);
    }
    QList<obs_source_t *> existing;
    for (auto *s : std::as_const(m_strips)) existing << s->source();
    if (existing == sources) return;
    qDeleteAll(m_strips);
    m_strips.clear();
    for (obs_source_t *s : sources) {
        auto *strip = new AudioStrip(s);
        strip->setObjectName("GlassPanel");
        connect(strip, &AudioStrip::soloToggled, this, [this](AudioStrip *who, bool on) {
            for (auto *o : std::as_const(m_strips)) if (o != who) o->setSoloMuted(on);
        });
        m_lay->insertWidget(m_lay->count() - 1, strip);
        m_strips << strip;
    }
}

void openAudioAdvanced(obs_source_t *src, QWidget *parent) {
    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Advanced audio — %1").arg(QString::fromUtf8(obs_source_get_name(src))));
    auto *form = new QFormLayout(&d);
    auto *sync = new QSpinBox; sync->setRange(-950, 20000); sync->setSuffix(" ms");
    sync->setValue(int(obs_source_get_sync_offset(src) / 1000000));
    auto *bal = new QSlider(Qt::Horizontal); bal->setRange(0, 100); bal->setValue(int(obs_source_get_balance_value(src) * 100));
    auto *mono = new QCheckBox(QObject::tr("Downmix to mono"));
    mono->setChecked(obs_source_get_flags(src) & OBS_SOURCE_FLAG_FORCE_MONO);
    form->addRow(QObject::tr("Sync offset"), sync);
    form->addRow(QObject::tr("Balance (L ↔ R)"), bal);
    form->addRow(QString(), mono);
    auto *tracks = new QWidget; auto *th = new QHBoxLayout(tracks); th->setContentsMargins(0, 0, 0, 0);
    QList<QCheckBox *> boxes;
    const uint32_t mixers = obs_source_get_audio_mixers(src);
    for (int i = 0; i < 6; ++i) { auto *cb = new QCheckBox(QString::number(i + 1)); cb->setChecked(mixers & (1u << i)); th->addWidget(cb); boxes << cb; }
    form->addRow(QObject::tr("Recording tracks"), tracks);
    auto *mon = new QComboBox;
    mon->addItems({QObject::tr("Monitor off"), QObject::tr("Monitor only (mute output)"), QObject::tr("Monitor and output")});
    mon->setCurrentIndex(int(obs_source_get_monitoring_type(src)));
    form->addRow(QObject::tr("Monitoring"), mon);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    obs_source_set_sync_offset(src, int64_t(sync->value()) * 1000000);
    obs_source_set_balance_value(src, float(bal->value()) / 100.f);
    uint32_t flags = obs_source_get_flags(src);
    flags = mono->isChecked() ? (flags | OBS_SOURCE_FLAG_FORCE_MONO) : (flags & ~uint32_t(OBS_SOURCE_FLAG_FORCE_MONO));
    obs_source_set_flags(src, flags);
    uint32_t m = 0;
    for (int i = 0; i < boxes.size(); ++i) if (boxes[i]->isChecked()) m |= (1u << i);
    obs_source_set_audio_mixers(src, m);
    obs_source_set_monitoring_type(src, obs_monitoring_type(mon->currentIndex()));
}

// ===================================================================== Transitions dock
static const QList<QPair<QString, QString>> &transitionChoices() {
    static const QList<QPair<QString, QString>> l = {
        {"fade_transition", QObject::tr("Fade")}, {"cut_transition", QObject::tr("Cut")},
        {"bc_luma_heart", QObject::tr("♡ Heart Iris")}, {"bc_luma_star", QObject::tr("★ Star Burst")},
        {"bc_luma_ribbon", QObject::tr("🎀 Ribbon Sweep")}, {"bc_luma_sparkle", QObject::tr("✦ Sparkle Dissolve")},
        {"fade_to_color_transition", QObject::tr("Fade to colour")}, {"swipe_transition", QObject::tr("Swipe")},
        {"slide_transition", QObject::tr("Slide")}, {"wipe_transition", QObject::tr("Luma wipe")},
        {"obs_stinger_transition", QObject::tr("Stinger (video)")}};
    return l;
}

TransitionsPanel::TransitionsPanel(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    m_combo = new QComboBox;
    const QStringList avail = Engine::instance().transitionTypes();
    for (const auto &[id, name] : transitionChoices()) {
        const bool ok = id.startsWith("bc_luma_") ? avail.contains("wipe_transition") : avail.contains(id);
        if (ok) m_combo->addItem(name, id);
    }
    auto *row = new QHBoxLayout;
    row->addWidget(m_combo, 1);
    auto *gear = tinyButton("⚙", tr("Transition properties"), this);
    row->addWidget(gear);
    lay->addLayout(row);
    auto *dur = new QHBoxLayout;
    dur->addWidget(new QLabel(tr("Duration")));
    m_duration = new QSpinBox; m_duration->setRange(50, 20000); m_duration->setSingleStep(50); m_duration->setSuffix(" ms");
    dur->addWidget(m_duration, 1);
    lay->addLayout(dur);
    auto *go = new QPushButton(tr("Transition ➜"));
    go->setProperty("accent", "pink");
    go->setToolTip(tr("Send the preview scene to program (studio mode)"));
    lay->addWidget(go);
    lay->addStretch();
    connect(m_combo, &QComboBox::activated, this, [this] { Engine::instance().setTransition(m_combo->currentData().toString()); });
    connect(m_duration, &QSpinBox::valueChanged, this, [](int v) { Engine::instance().setTransitionDurationMs(v); });
    connect(gear, &QToolButton::clicked, this, [this] { if (obs_source_t *t = Engine::instance().transition()) (new PropertiesDialog(t, this))->show(); });
    connect(go, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.transitionTo(e.previewScene()); });
    sync();
}

void TransitionsPanel::sync() {
    QSignalBlocker b1(m_combo), b2(m_duration);
    m_duration->setValue(Engine::instance().transitionDurationMs());
    const int idx = m_combo->findData(Engine::instance().transitionId());
    if (idx >= 0) m_combo->setCurrentIndex(idx);
}

// ===================================================================== Stats dock
StatsPanel::StatsPanel(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    m_engine = new QLabel;
    m_engine->setTextFormat(Qt::RichText);
    m_engine->setWordWrap(true);
    lay->addWidget(m_engine);
    m_outputs = new QTableWidget(0, 4);
    m_outputs->setHorizontalHeaderLabels({tr("Output"), tr("Status"), tr("kbps"), tr("Dropped")});
    m_outputs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_outputs->verticalHeader()->hide();
    m_outputs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_outputs, 1);
}

void StatsPanel::refresh() {
    if (!isVisible()) return;
    auto &e = Engine::instance();
    const auto st = e.stats();
    const double lagPct = st.renderTotal ? 100.0 * st.renderLagged / st.renderTotal : 0;
    const double skipPct = st.encoderTotal ? 100.0 * st.encoderSkipped / st.encoderTotal : 0;
    m_engine->setText(tr("<b>%1</b> fps · render %2 ms<br>Render lag: %3 (%4%)<br>Encoder skipped: %5 (%6%)")
        .arg(st.activeFps, 0, 'f', 2).arg(st.avgFrameMs, 0, 'f', 2).arg(st.renderLagged).arg(lagPct, 0, 'f', 1)
        .arg(st.encoderSkipped).arg(skipPct, 0, 'f', 1));
    QList<QStringList> rows;
    for (const QString &id : e.activeDestinations()) {
        const auto s = e.destinationStatus(id);
        QString name = id;
        for (const auto &d : PlatformHub::instance().destinations()) if (d.id == id) name = d.name;
        rows << QStringList{name, outputStateName(s.state), QString::number(int(s.kbps)),
                            s.totalFrames ? QStringLiteral("%1 (%2%)").arg(s.droppedFrames).arg(100.0 * s.droppedFrames / s.totalFrames, 0, 'f', 1) : "0"};
    }
    if (e.isRecording()) rows << QStringList{tr("Recording"), e.isRecordingPaused() ? tr("Paused") : tr("Recording"), "—", "—"};
    if (e.isReplayBufferActive()) rows << QStringList{tr("Replay buffer"), tr("Active"), "—", "—"};
    if (e.isVirtualCamActive()) rows << QStringList{tr("Virtual camera"), tr("Active"), "—", "—"};
    m_outputs->setRowCount(int(rows.size()));
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < 4; ++c) m_outputs->setItem(r, c, new QTableWidgetItem(rows[r][c]));
}

// ===================================================================== Filters
FiltersDialog::FiltersDialog(obs_source_t *src, QWidget *parent) : QDialog(parent), m_src(src) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Filters & effects — %1").arg(QString::fromUtf8(obs_source_get_name(src))));
    resize(860, 560);
    auto *lay = new QHBoxLayout(this);
    auto *left = new QVBoxLayout;
    m_list = new QListWidget;
    left->addWidget(m_list, 1);
    auto *row = new QHBoxLayout;
    auto *add = tinyButton("＋", tr("Add filter"), this);
    auto *del = tinyButton("－", tr("Remove filter"), this);
    auto *up = tinyButton("▲", tr("Move up"), this);
    auto *down = tinyButton("▼", tr("Move down"), this);
    for (auto *b : {add, del, up, down}) row->addWidget(b);
    row->addStretch();
    left->addLayout(row);
    auto *presetRow = new QHBoxLayout;
    auto *save = new QPushButton(tr("Save preset…"));
    auto *load = new QPushButton(tr("Load preset…"));
    presetRow->addWidget(save); presetRow->addWidget(load);
    left->addLayout(presetRow);
    lay->addLayout(left, 1);
    m_propsHost = new QWidget;
    new QVBoxLayout(m_propsHost);
    lay->addWidget(m_propsHost, 2);

    const uint32_t flags = obs_source_get_output_flags(src);
    connect(add, &QToolButton::clicked, this, [this, add, flags] {
        QMenu m(this);
        auto addList = [&](bool audio) {
            for (const auto &f : Engine::instance().filterTypes(audio)) {
                m.addAction(f.name, this, [this, f] {
                    const QString n = QInputDialog::getText(this, tr("Add filter"), tr("Name:"), QLineEdit::Normal, f.name);
                    if (n.isEmpty()) return;
                    OBSSourceAutoRelease filt = obs_source_create(f.id.toUtf8().constData(), n.toUtf8().constData(), nullptr, nullptr);
                    if (!filt) { QMessageBox::warning(this, tr("Add filter"), tr("This filter isn't available on your system.")); return; }
                    obs_source_filter_add(m_src, filt);
                    reload();
                    m_list->setCurrentRow(m_list->count() - 1);
                });
            }
        };
        if (flags & OBS_SOURCE_AUDIO) { m.addSection(tr("Audio")); addList(true); }
        if (flags & OBS_SOURCE_VIDEO) { m.addSection(tr("Video & effects")); addList(false); }
        m.exec(add->mapToGlobal(QPoint(0, add->height())));
    });
    auto current = [this]() -> obs_source_t * {
        auto *it = m_list->currentItem();
        return it ? obs_source_get_filter_by_name(m_src, it->text().toUtf8().constData()) : nullptr;   // returns new ref
    };
    connect(del, &QToolButton::clicked, this, [this, current] { if (obs_source_t *f = current()) { obs_source_filter_remove(m_src, f); obs_source_release(f); reload(); } });
    connect(up, &QToolButton::clicked, this, [this, current] { if (obs_source_t *f = current()) { const int r = m_list->currentRow(); obs_source_filter_set_order(m_src, f, OBS_ORDER_MOVE_UP); obs_source_release(f); reload(); m_list->setCurrentRow(qMax(0, r - 1)); } });
    connect(down, &QToolButton::clicked, this, [this, current] { if (obs_source_t *f = current()) { const int r = m_list->currentRow(); obs_source_filter_set_order(m_src, f, OBS_ORDER_MOVE_DOWN); obs_source_release(f); reload(); m_list->setCurrentRow(qMin(m_list->count() - 1, r + 1)); } });
    connect(m_list, &QListWidget::currentRowChanged, this, [this, current] { obs_source_t *f = current(); showFilter(f); if (f) obs_source_release(f); });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        if (obs_source_t *f = obs_source_get_filter_by_name(m_src, it->text().toUtf8().constData())) {
            obs_source_set_enabled(f, it->checkState() == Qt::Checked);
            obs_source_release(f);
        }
    });
    connect(save, &QPushButton::clicked, this, &FiltersDialog::savePreset);
    connect(load, &QPushButton::clicked, this, &FiltersDialog::loadPreset);
    reload();
    if (m_list->count()) m_list->setCurrentRow(0);
}

void FiltersDialog::reload() {
    QSignalBlocker b(m_list);
    m_list->clear();
    obs_source_enum_filters(m_src, [](obs_source_t *, obs_source_t *f, void *p) {
        auto *l = static_cast<QListWidget *>(p);
        auto *it = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(f)));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(obs_source_enabled(f) ? Qt::Checked : Qt::Unchecked);
        l->addItem(it);
    }, m_list);
    if (m_list->count() == 0) showFilter(nullptr);
}

void FiltersDialog::showFilter(obs_source_t *f) {
    QLayout *l = m_propsHost->layout();
    while (QLayoutItem *it = l->takeAt(0)) { delete it->widget(); delete it; }
    if (f) l->addWidget(new PropertiesView(f, m_propsHost));
    else {
        auto *hint = new QLabel(tr("No filters yet ✦\nClick ＋ to add noise suppression, a compressor, colour correction, chroma key, LUTs and more."));
        hint->setWordWrap(true); hint->setAlignment(Qt::AlignCenter);
        l->addWidget(hint);
    }
}

void FiltersDialog::savePreset() {
    const QString dir = Paths::ensure(Paths::dataRoot() + "/presets");
    const QString n = QInputDialog::getText(this, tr("Save filter preset"), tr("Preset name:"));
    if (n.trimmed().isEmpty()) return;
    QJsonArray arr;
    obs_source_enum_filters(m_src, [](obs_source_t *, obs_source_t *f, void *p) {
        OBSDataAutoRelease s = obs_source_get_settings(f);
        static_cast<QJsonArray *>(p)->append(QJsonObject{
            {"id", QString::fromUtf8(obs_source_get_id(f))}, {"name", QString::fromUtf8(obs_source_get_name(f))},
            {"enabled", obs_source_enabled(f)},
            {"settings", QJsonDocument::fromJson(obs_data_get_json(s)).object()}});
    }, &arr);
    QFile out(dir + "/" + QString(n).replace(QRegularExpression("[^\\w -]"), "_") + ".json");
    if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(arr).toJson());
}

void FiltersDialog::loadPreset() {
    const QString f = QFileDialog::getOpenFileName(this, tr("Load filter preset"), Paths::ensure(Paths::dataRoot() + "/presets"), "*.json");
    if (f.isEmpty()) return;
    QFile in(f);
    if (!in.open(QIODevice::ReadOnly)) return;
    for (const auto v : QJsonDocument::fromJson(in.readAll()).array()) {
        const QJsonObject o = v.toObject();
        OBSDataAutoRelease s = obs_data_create_from_json(QJsonDocument(o.value("settings").toObject()).toJson().constData());
        QString name = o.value("name").toString();
        for (int i = 2; obs_source_t *ex = obs_source_get_filter_by_name(m_src, name.toUtf8().constData()); ++i) {
            obs_source_release(ex);
            name = o.value("name").toString() + " " + QString::number(i);
        }
        OBSSourceAutoRelease filt = obs_source_create(o.value("id").toString().toUtf8().constData(), name.toUtf8().constData(), s, nullptr);
        if (!filt) continue;
        obs_source_set_enabled(filt, o.value("enabled").toBool(true));
        obs_source_filter_add(m_src, filt);
    }
    reload();
}

// ===================================================================== Transform
TransformDialog::TransformDialog(obs_sceneitem_t *item, PreviewWidget *preview, QWidget *parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Edit transform"));
    OBSSceneItem keep(item);
    const ItemTransform before = ItemTransform::read(item);
    auto *form = new QFormLayout(this);
    auto dbl = [](double lo, double hi, double v, int dec = 1) { auto *s = new QDoubleSpinBox; s->setRange(lo, hi); s->setDecimals(dec); s->setValue(v); return s; };
    auto *px = dbl(-20000, 20000, before.pos.x), *py = dbl(-20000, 20000, before.pos.y);
    auto *rot = dbl(-360, 360, before.rot);
    auto *sx = dbl(-100, 100, before.scale.x, 4), *sy = dbl(-100, 100, before.scale.y, 4);
    auto *bt = new QComboBox;
    bt->addItems({tr("No bounds"), tr("Stretch to bounds"), tr("Scale to inner bounds"), tr("Scale to outer bounds"),
                  tr("Scale to width"), tr("Scale to height"), tr("Maximum size only")});
    bt->setCurrentIndex(int(before.boundsType));
    auto *bw = dbl(0, 20000, before.bounds.x), *bh = dbl(0, 20000, before.bounds.y);
    auto *cl = new QSpinBox, *ct = new QSpinBox, *cr = new QSpinBox, *cb = new QSpinBox;
    for (auto *s : {cl, ct, cr, cb}) s->setRange(0, 20000);
    cl->setValue(before.crop.left); ct->setValue(before.crop.top); cr->setValue(before.crop.right); cb->setValue(before.crop.bottom);
    auto pair = [](QWidget *a, QWidget *b) { auto *w = new QWidget; auto *h = new QHBoxLayout(w); h->setContentsMargins(0, 0, 0, 0); h->addWidget(a); h->addWidget(b); return w; };
    form->addRow(tr("Position (x, y)"), pair(px, py));
    form->addRow(tr("Rotation (°)"), rot);
    form->addRow(tr("Scale (x, y)"), pair(sx, sy));
    form->addRow(tr("Bounding box"), bt);
    form->addRow(tr("Bounds size (w, h)"), pair(bw, bh));
    form->addRow(tr("Crop (left, top)"), pair(cl, ct));
    form->addRow(tr("Crop (right, bottom)"), pair(cr, cb));
    auto apply = [=] {
        ItemTransform t = before;
        t.pos = {float(px->value()), float(py->value())};
        t.rot = float(rot->value());
        t.scale = {float(sx->value()), float(sy->value())};
        t.boundsType = obs_bounds_type(bt->currentIndex());
        t.bounds = {float(bw->value()), float(bh->value())};
        t.crop = {cl->value(), ct->value(), cr->value(), cb->value()};
        t.apply(keep);
    };
    for (auto *s : {px, py, rot, sx, sy, bw, bh}) connect(s, &QDoubleSpinBox::valueChanged, this, apply);
    for (auto *s : {cl, ct, cr, cb}) connect(s, &QSpinBox::valueChanged, this, apply);
    connect(bt, &QComboBox::currentIndexChanged, this, apply);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, [=] { preview->pushTransformUndo(keep, before, tr("Edit transform")); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, [=] { before.apply(keep); reject(); });
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [=] {
        px->setValue(0); py->setValue(0); rot->setValue(0); sx->setValue(1); sy->setValue(1); bt->setCurrentIndex(0);
        for (auto *s : {cl, ct, cr, cb}) s->setValue(0);
    });
}

} // namespace bc
