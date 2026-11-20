#include "ui/PropertiesView.h"
#include "engine/Engine.h"
#include "core/Settings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace bc {

static QColor fromObsColor(long long v) {   // OBS stores 0xAABBGGRR
    return QColor(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF), int((v >> 24) & 0xFF));
}
static long long toObsColor(const QColor &c) {
    return (static_cast<long long>(c.alpha()) << 24) | (c.blue() << 16) | (c.green() << 8) | c.red();
}
static void styleSwatch(QPushButton *b, const QColor &c) {
    b->setText(c.name(QColor::HexArgb).toUpper());
    b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;}").arg(c.name(),
                     c.lightness() > 140 ? "#0B0A0D" : "#FFFFFF"));
}

PropertiesView::PropertiesView(obs_source_t *source, QWidget *parent) : QScrollArea(parent), m_source(source) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    m_settings = obs_source_get_settings(source);
    rebuild();
}

PropertiesView::~PropertiesView() {
    if (m_props) obs_properties_destroy(m_props);
    obs_data_release(m_settings);
}

void PropertiesView::scheduleRebuild() {
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] { m_rebuildPending = false; rebuild(); });
}

void PropertiesView::commit(const QString &propName) {
    obs_source_update(m_source, m_settings);
    if (obs_property_t *p = obs_properties_get(m_props, propName.toUtf8().constData()))
        if (obs_property_modified(p, m_settings)) scheduleRebuild();
    Q_EMIT changed();
}

bool PropertiesView::buildGradientEditor(QFormLayout *form) {
    // Gradient sources are image sources generated from two colours (see Engine::addSource).
    if (QString::fromUtf8(obs_source_get_id(m_source)) != "image_source" || !obs_data_has_user_value(m_settings, "bc_c1"))
        return false;
    for (const char *key : {"bc_c1", "bc_c2"}) {
        auto *btn = new QPushButton;
        styleSwatch(btn, QColor(obs_data_get_string(m_settings, key)));
        connect(btn, &QPushButton::clicked, this, [this, btn, k = QByteArray(key)] {
            const QColor c = QColorDialog::getColor(QColor(obs_data_get_string(m_settings, k)), this, tr("Gradient colour"));
            if (!c.isValid()) return;
            obs_data_set_string(m_settings, k, c.name().toUtf8().constData());
            styleSwatch(btn, c);
            const auto &s = Settings::instance();
            const QString file = Engine::makeGradientImage(obs_data_get_string(m_settings, "bc_c1"),
                                                           obs_data_get_string(m_settings, "bc_c2"),
                                                           s.i("video.baseWidth"), s.i("video.baseHeight"));
            obs_data_set_string(m_settings, "file", file.toUtf8().constData());
            commit("file");
        });
        form->addRow(QString(key) == "bc_c1" ? tr("Top-left colour") : tr("Bottom-right colour"), btn);
    }
    return true;
}

void PropertiesView::rebuild() {
    if (m_props) obs_properties_destroy(m_props);
    m_props = obs_source_properties(m_source);
    auto *host = new QWidget;
    auto *form = new QFormLayout(host);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    if (!buildGradientEditor(form)) {
        if (!m_props) {
            form->addRow(new QLabel(tr("This item has no settings.")));
        } else {
            for (obs_property_t *p = obs_properties_first(m_props); p; obs_property_next(&p)) addProperty(p, form);
        }
    }
    setWidget(host);   // deletes the previous host widget
}

void PropertiesView::addProperty(obs_property_t *p, QFormLayout *form) {
    if (!obs_property_visible(p)) return;
    const QString name = QString::fromUtf8(obs_property_name(p));
    const QByteArray key = name.toUtf8();
    const QString label = QString::fromUtf8(obs_property_description(p) ? obs_property_description(p) : "");
    const QString tip = QString::fromUtf8(obs_property_long_description(p) ? obs_property_long_description(p) : "");
    QWidget *w = nullptr;

    switch (obs_property_get_type(p)) {
    case OBS_PROPERTY_BOOL: {
        auto *cb = new QCheckBox(label);
        cb->setChecked(obs_data_get_bool(m_settings, key));
        connect(cb, &QCheckBox::toggled, this, [this, key, name](bool v) { obs_data_set_bool(m_settings, key, v); commit(name); });
        form->addRow(QString(), cb);
        cb->setEnabled(obs_property_enabled(p));
        cb->setToolTip(tip);
        return;
    }
    case OBS_PROPERTY_INT: {
        auto *sp = new QSpinBox;
        sp->setRange(obs_property_int_min(p), obs_property_int_max(p));
        sp->setSingleStep(qMax(1, obs_property_int_step(p)));
        sp->setSuffix(QString::fromUtf8(obs_property_int_suffix(p) ? obs_property_int_suffix(p) : ""));
        sp->setValue(int(obs_data_get_int(m_settings, key)));
        if (obs_property_int_type(p) == OBS_NUMBER_SLIDER) {
            auto *row = new QWidget; auto *h = new QHBoxLayout(row); h->setContentsMargins(0, 0, 0, 0);
            auto *sl = new QSlider(Qt::Horizontal);
            sl->setRange(sp->minimum(), sp->maximum()); sl->setSingleStep(sp->singleStep()); sl->setValue(sp->value());
            connect(sl, &QSlider::valueChanged, sp, &QSpinBox::setValue);
            connect(sp, &QSpinBox::valueChanged, sl, &QSlider::setValue);
            h->addWidget(sl, 1); h->addWidget(sp);
            w = row;
        } else w = sp;
        connect(sp, &QSpinBox::valueChanged, this, [this, key, name](int v) { obs_data_set_int(m_settings, key, v); commit(name); });
        break;
    }
    case OBS_PROPERTY_FLOAT: {
        auto *sp = new QDoubleSpinBox;
        sp->setRange(obs_property_float_min(p), obs_property_float_max(p));
        sp->setSingleStep(obs_property_float_step(p));
        sp->setDecimals(obs_property_float_step(p) < 0.01 ? 4 : 2);
        sp->setSuffix(QString::fromUtf8(obs_property_float_suffix(p) ? obs_property_float_suffix(p) : ""));
        sp->setValue(obs_data_get_double(m_settings, key));
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key, name](double v) { obs_data_set_double(m_settings, key, v); commit(name); });
        w = sp;
        break;
    }
    case OBS_PROPERTY_TEXT: {
        const auto tt = obs_property_text_type(p);
        if (tt == OBS_TEXT_INFO) { auto *l = new QLabel(QString::fromUtf8(obs_data_get_string(m_settings, key)).isEmpty() ? label : QString::fromUtf8(obs_data_get_string(m_settings, key))); l->setWordWrap(true); form->addRow(l); return; }
        if (tt == OBS_TEXT_MULTILINE) {
            auto *te = new QPlainTextEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
            te->setMaximumHeight(110);
            connect(te, &QPlainTextEdit::textChanged, this, [this, te, key, name] { obs_data_set_string(m_settings, key, te->toPlainText().toUtf8().constData()); commit(name); });
            w = te;
        } else {
            auto *le = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
            if (tt == OBS_TEXT_PASSWORD) le->setEchoMode(QLineEdit::Password);
            connect(le, &QLineEdit::textEdited, this, [this, key, name](const QString &v) { obs_data_set_string(m_settings, key, v.toUtf8().constData()); commit(name); });
            w = le;
        }
        break;
    }
    case OBS_PROPERTY_PATH: {
        auto *row = new QWidget; auto *h = new QHBoxLayout(row); h->setContentsMargins(0, 0, 0, 0);
        auto *le = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
        auto *btn = new QPushButton(tr("Browse…"));
        h->addWidget(le, 1); h->addWidget(btn);
        const bool isDir = obs_property_path_type(p) == OBS_PATH_DIRECTORY;
        const bool save = obs_property_path_type(p) == OBS_PATH_FILE_SAVE;
        const QString filter = QString::fromUtf8(obs_property_path_filter(p) ? obs_property_path_filter(p) : "");
        connect(le, &QLineEdit::editingFinished, this, [this, le, key, name] { obs_data_set_string(m_settings, key, le->text().toUtf8().constData()); commit(name); });
        connect(btn, &QPushButton::clicked, this, [this, le, key, name, isDir, save, filter, label] {
            QString f = isDir ? QFileDialog::getExistingDirectory(this, label, le->text())
                      : save ? QFileDialog::getSaveFileName(this, label, le->text(), filter)
                             : QFileDialog::getOpenFileName(this, label, le->text(), filter);
            if (f.isEmpty()) return;
            le->setText(f);
            obs_data_set_string(m_settings, key, f.toUtf8().constData());
            commit(name);
        });
        w = row;
        break;
    }
    case OBS_PROPERTY_LIST: {
        auto *cb = new QComboBox;
        const auto fmt = obs_property_list_format(p);
        cb->setEditable(obs_property_list_type(p) == OBS_COMBO_TYPE_EDITABLE);
        int current = -1;
        for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
            const QString itemName = QString::fromUtf8(obs_property_list_item_name(p, i));
            QVariant v;
            bool match = false;
            if (fmt == OBS_COMBO_FORMAT_INT) { v = qlonglong(obs_property_list_item_int(p, i)); match = obs_data_get_int(m_settings, key) == v.toLongLong(); }
            else if (fmt == OBS_COMBO_FORMAT_FLOAT) { v = obs_property_list_item_float(p, i); match = qFuzzyCompare(obs_data_get_double(m_settings, key) + 1, v.toDouble() + 1); }
            else if (fmt == OBS_COMBO_FORMAT_BOOL) { v = obs_property_list_item_bool(p, i); match = obs_data_get_bool(m_settings, key) == v.toBool(); }
            else { v = QString::fromUtf8(obs_property_list_item_string(p, i)); match = QString::fromUtf8(obs_data_get_string(m_settings, key)) == v.toString(); }
            cb->addItem(itemName, v);
            if (obs_property_list_item_disabled(p, i))
                if (auto *model = qobject_cast<QStandardItemModel *>(cb->model()))
                    model->item(cb->count() - 1)->setEnabled(false);
            if (match) current = int(i);
        }
        if (current >= 0) cb->setCurrentIndex(current);
        else if (fmt == OBS_COMBO_FORMAT_STRING && cb->isEditable()) cb->setEditText(QString::fromUtf8(obs_data_get_string(m_settings, key)));
        connect(cb, &QComboBox::currentIndexChanged, this, [this, cb, key, name, fmt](int idx) {
            const QVariant v = cb->itemData(idx);
            if (fmt == OBS_COMBO_FORMAT_INT) obs_data_set_int(m_settings, key, v.toLongLong());
            else if (fmt == OBS_COMBO_FORMAT_FLOAT) obs_data_set_double(m_settings, key, v.toDouble());
            else if (fmt == OBS_COMBO_FORMAT_BOOL) obs_data_set_bool(m_settings, key, v.toBool());
            else obs_data_set_string(m_settings, key, v.toString().toUtf8().constData());
            commit(name);
        });
        w = cb;
        break;
    }
    case OBS_PROPERTY_COLOR:
    case OBS_PROPERTY_COLOR_ALPHA: {
        const bool alpha = obs_property_get_type(p) == OBS_PROPERTY_COLOR_ALPHA;
        auto *btn = new QPushButton;
        QColor c = fromObsColor(obs_data_get_int(m_settings, key));
        if (!alpha) c.setAlpha(255);
        styleSwatch(btn, c);
        connect(btn, &QPushButton::clicked, this, [this, btn, key, name, alpha, label] {
            QColor cur = fromObsColor(obs_data_get_int(m_settings, key));
            QColor c = QColorDialog::getColor(cur, this, label, alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
            if (!c.isValid()) return;
            if (!alpha) c.setAlpha(255);
            obs_data_set_int(m_settings, key, toObsColor(c));
            styleSwatch(btn, c);
            commit(name);
        });
        w = btn;
        break;
    }
    case OBS_PROPERTY_BUTTON: {
        auto *btn = new QPushButton(label);
        connect(btn, &QPushButton::clicked, this, [this, key] {
            if (obs_property_t *pp = obs_properties_get(m_props, key))
                if (obs_property_button_clicked(pp, m_source)) scheduleRebuild();
        });
        form->addRow(QString(), btn);
        btn->setEnabled(obs_property_enabled(p));
        return;
    }
    case OBS_PROPERTY_FONT: {
        auto *btn = new QPushButton;
        OBSDataAutoRelease f = obs_data_get_obj(m_settings, key);
        auto describe = [](obs_data_t *fd) {
            return fd ? QStringLiteral("%1 %2pt").arg(QString::fromUtf8(obs_data_get_string(fd, "face"))).arg(obs_data_get_int(fd, "size"))
                      : QObject::tr("Choose font…");
        };
        btn->setText(describe(f));
        connect(btn, &QPushButton::clicked, this, [this, btn, key, name, describe] {
            OBSDataAutoRelease cur = obs_data_get_obj(m_settings, key);
            QFont start(cur ? QString::fromUtf8(obs_data_get_string(cur, "face")) : "Palatino Linotype",
                        cur ? int(obs_data_get_int(cur, "size")) : 48);
            bool ok = false;
            const QFont font = QFontDialog::getFont(&ok, start, this);
            if (!ok) return;
            OBSDataAutoRelease fd = obs_data_create();
            obs_data_set_string(fd, "face", font.family().toUtf8().constData());
            obs_data_set_string(fd, "style", font.styleName().toUtf8().constData());
            obs_data_set_int(fd, "size", font.pointSize() > 0 ? font.pointSize() : 48);
            obs_data_set_int(fd, "flags", (font.bold() ? 1 : 0) | (font.italic() ? 2 : 0) | (font.underline() ? 4 : 0) | (font.strikeOut() ? 8 : 0));
            obs_data_set_obj(m_settings, key, fd);
            btn->setText(describe(fd));
            commit(name);
        });
        w = btn;
        break;
    }
    case OBS_PROPERTY_GROUP: {
        auto *box = new QGroupBox(label);
        auto *inner = new QFormLayout(box);
        if (obs_property_group_type(p) == OBS_GROUP_CHECKABLE) {
            box->setCheckable(true);
            box->setChecked(obs_data_get_bool(m_settings, key));
            connect(box, &QGroupBox::toggled, this, [this, key, name](bool v) { obs_data_set_bool(m_settings, key, v); commit(name); });
        }
        obs_properties_t *content = obs_property_group_content(p);
        for (obs_property_t *c = obs_properties_first(content); c; obs_property_next(&c)) addProperty(c, inner);
        form->addRow(box);
        return;
    }
    case OBS_PROPERTY_EDITABLE_LIST: {
        // One entry per line (e.g. image slideshow / VLC playlist files).
        auto *te = new QPlainTextEdit;
        OBSDataArrayAutoRelease arr = obs_data_get_array(m_settings, key);
        QStringList lines;
        for (size_t i = 0; arr && i < obs_data_array_count(arr); ++i) {
            OBSDataAutoRelease it = obs_data_array_item(arr, i);
            lines << QString::fromUtf8(obs_data_get_string(it, "value"));
        }
        te->setPlainText(lines.join('\n'));
        te->setMaximumHeight(120);
        te->setPlaceholderText(tr("One file path or URL per line"));
        connect(te, &QPlainTextEdit::textChanged, this, [this, te, key, name] {
            OBSDataArrayAutoRelease a = obs_data_array_create();
            for (const QString &l : te->toPlainText().split('\n', Qt::SkipEmptyParts)) {
                OBSDataAutoRelease it = obs_data_create();
                obs_data_set_string(it, "value", l.trimmed().toUtf8().constData());
                obs_data_set_bool(it, "selected", false);
                obs_data_set_bool(it, "hidden", false);
                obs_data_array_push_back(a, it);
            }
            obs_data_set_array(m_settings, key, a);
            commit(name);
        });
        w = te;
        break;
    }
    default: {
        auto *l = new QLabel(tr("(edited automatically)"));
        l->setObjectName("Muted");
        w = l;
    }
    }
    if (w) {
        w->setEnabled(obs_property_enabled(p));
        w->setToolTip(tip);
        form->addRow(label, w);
    }
}

// ---------------------------------------------------------------- dialog
PropertiesDialog::PropertiesDialog(obs_source_t *source, QWidget *parent) : QDialog(parent), m_source(source) {
    setWindowTitle(tr("Properties — %1").arg(QString::fromUtf8(obs_source_get_name(source))));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(560, 620);
    OBSDataAutoRelease cur = obs_source_get_settings(source);
    m_original = obs_data_create_from_json(obs_data_get_json(cur));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new PropertiesView(source, this), 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setProperty("accent", "primary");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void PropertiesDialog::reject() {
    // Restore: clear then re-apply the original settings.
    OBSDataAutoRelease s = obs_source_get_settings(m_source);
    obs_data_clear(s);
    obs_data_apply(s, m_original);
    obs_source_update(m_source, s);
    QDialog::reject();
}

} // namespace bc
