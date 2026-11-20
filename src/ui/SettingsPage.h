#pragma once
#include <QWidget>

class QListWidget;
class QStackedWidget;

namespace bc {

// OBS-style settings: categories down the left, one page each, OK / Cancel / Apply.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void load();
    // Adds a page owned elsewhere (e.g. the Stream destinations editor) as a category.
    void insertCategory(int index, QWidget *page, const QString &label, const QString &glyph);
    void showCategory(const QString &label);
    void orderCategories(const QStringList &labels);   // OBS order; unknown labels keep their place after
Q_SIGNALS:
    void rerunWizard();
    void themeChanged();
    void closeRequested();
private:
    void apply();
    void addCategory(QWidget *page, const QString &label, const QString &glyph);
    QListWidget *m_categories;
    QStackedWidget *m_pages;
    QHash<QString, QWidget *> m_fields;   // settings key → editor
};

void openMicTest(QWidget *parent);
void applyProcessPriority();

} // namespace bc
