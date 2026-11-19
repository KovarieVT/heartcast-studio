#pragma once
#include <QDialog>
#include <QScrollArea>
#include <obs.hpp>

class QFormLayout;

namespace bc {

// Builds a live settings editor from any libobs object's obs_properties
// (sources, filters, transitions, widgets). Every control writes straight to
// the object's settings and calls obs_source_update, so changes preview live.
class PropertiesView : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertiesView(obs_source_t *source, QWidget *parent = nullptr);
    ~PropertiesView() override;
    void rebuild();

Q_SIGNALS:
    void changed();

private:
    void addProperty(obs_property_t *p, QFormLayout *form);
    void commit(const QString &propName);
    void scheduleRebuild();
    bool buildGradientEditor(QFormLayout *form);

    OBSSource m_source;
    obs_data_t *m_settings = nullptr;
    obs_properties_t *m_props = nullptr;
    bool m_rebuildPending = false;
};

// Modal-less dialog wrapper with OK / Cancel (Cancel restores original settings).
class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(obs_source_t *source, QWidget *parent = nullptr);
    void reject() override;
private:
    OBSSource m_source;
    OBSDataAutoRelease m_original;
};

} // namespace bc
