#pragma once
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <functional>

class QShortcut;
class QWidget;

namespace bc {

struct HotkeyAction {
    QString id, label, group;
    std::function<void()> fn;
};

// Global (system-wide) hotkeys on Windows via RegisterHotKey, so they work
// while a game has focus. Elsewhere falls back to application shortcuts.
// Detects in-app duplicates and keys already owned by another program.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static HotkeyManager &instance();
    void setHostWindow(QWidget *w) { m_host = w; }

    void registerAction(const QString &id, const QString &label, const QString &group, std::function<void()> fn);
    QList<HotkeyAction> actions() const;

    QKeySequence binding(const QString &actionId) const;
    // Returns an empty string on success, otherwise a human-readable conflict.
    QString setBinding(const QString &actionId, const QKeySequence &seq);
    QString conflictFor(const QString &actionId, const QKeySequence &seq) const;
    void applyAll();
    QStringList failedRegistrations() const { return m_failed; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    // exposed for tests: Qt key → Windows virtual-key code (0 = unsupported)
    static unsigned toVirtualKey(int qtKey);

Q_SIGNALS:
    void triggered(const QString &actionId);

private:
    HotkeyManager() = default;
    void unregisterAll();
    QList<HotkeyAction> m_actions;
    QHash<int, QString> m_nativeIds;   // RegisterHotKey id → action
    QHash<QString, QShortcut *> m_shortcuts;
    QStringList m_failed;
    QWidget *m_host = nullptr;
    int m_nextId = 0x4243;
};

} // namespace bc
