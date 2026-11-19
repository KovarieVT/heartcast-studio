#pragma once
#include <QWidget>

class QTabWidget;

namespace bc {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void load();
Q_SIGNALS:
    void rerunWizard();
    void themeChanged();
private:
    void apply();
    QTabWidget *m_tabs;
    QHash<QString, QWidget *> m_fields;   // settings key → editor
};

void openMicTest(QWidget *parent);
void applyProcessPriority();

} // namespace bc
