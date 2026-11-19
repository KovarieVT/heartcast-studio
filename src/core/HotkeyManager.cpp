#include "core/HotkeyManager.h"
#include "core/Settings.h"
#include <QShortcut>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bc {

HotkeyManager &HotkeyManager::instance() {
    static HotkeyManager h;
    return h;
}

void HotkeyManager::registerAction(const QString &id, const QString &label, const QString &group, std::function<void()> fn) {
    for (auto &a : m_actions) if (a.id == id) { a.fn = std::move(fn); return; }
    m_actions << HotkeyAction{id, label, group, std::move(fn)};
}

QList<HotkeyAction> HotkeyManager::actions() const { return m_actions; }

QKeySequence HotkeyManager::binding(const QString &id) const {
    return QKeySequence(Settings::instance().s("hotkeys." + id), QKeySequence::PortableText);
}

QString HotkeyManager::conflictFor(const QString &id, const QKeySequence &seq) const {
    if (seq.isEmpty()) return {};
    for (const auto &a : m_actions)
        if (a.id != id && binding(a.id) == seq) return tr("Already used by \"%1\"").arg(a.label);
    if (seq.count() > 1) return tr("Only single key combinations are supported");
    if (toVirtualKey(seq[0].key()) == 0) return tr("This key can't be used as a global hotkey");
    return {};
}

QString HotkeyManager::setBinding(const QString &id, const QKeySequence &seq) {
    const QString c = conflictFor(id, seq);
    if (!c.isEmpty()) return c;
    Settings::instance().set("hotkeys." + id, seq.toString(QKeySequence::PortableText));
    Settings::instance().save();
    applyAll();
    if (m_failed.contains(id)) return tr("Windows refused this combination — another program is already using it.");
    return {};
}

unsigned HotkeyManager::toVirtualKey(int k) {
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return unsigned('A' + (k - Qt::Key_A));
    if (k >= Qt::Key_0 && k <= Qt::Key_9) return unsigned('0' + (k - Qt::Key_0));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) return 0x70u + unsigned(k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Space: return 0x20; case Qt::Key_PageUp: return 0x21; case Qt::Key_PageDown: return 0x22;
    case Qt::Key_End: return 0x23; case Qt::Key_Home: return 0x24; case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26; case Qt::Key_Right: return 0x27; case Qt::Key_Down: return 0x28;
    case Qt::Key_Insert: return 0x2D; case Qt::Key_Delete: return 0x2E; case Qt::Key_Pause: return 0x13;
    case Qt::Key_ScrollLock: return 0x91; case Qt::Key_Print: return 0x2C;
    case Qt::Key_Minus: return 0xBD; case Qt::Key_Equal: return 0xBB; case Qt::Key_Comma: return 0xBC;
    case Qt::Key_Period: return 0xBE; case Qt::Key_Slash: return 0xBF; case Qt::Key_Semicolon: return 0xBA;
    case Qt::Key_BracketLeft: return 0xDB; case Qt::Key_BracketRight: return 0xDD; case Qt::Key_Backslash: return 0xDC;
    case Qt::Key_QuoteLeft: return 0xC0; case Qt::Key_Apostrophe: return 0xDE;
    case Qt::Key_MediaPlay: return 0xB3; case Qt::Key_MediaNext: return 0xB0; case Qt::Key_MediaPrevious: return 0xB1;
    default: return 0;
    }
}

void HotkeyManager::unregisterAll() {
#ifdef _WIN32
    for (int id : m_nativeIds.keys()) UnregisterHotKey(nullptr, id);
#endif
    m_nativeIds.clear();
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
}

void HotkeyManager::applyAll() {
    unregisterAll();
    m_failed.clear();
    for (const auto &a : m_actions) {
        const QKeySequence seq = binding(a.id);
        if (seq.isEmpty()) continue;
#ifdef _WIN32
        const QKeyCombination kc = seq[0];
        const unsigned vk = toVirtualKey(kc.key());
        UINT mods = MOD_NOREPEAT;
        if (kc.keyboardModifiers() & Qt::ControlModifier) mods |= MOD_CONTROL;
        if (kc.keyboardModifiers() & Qt::ShiftModifier) mods |= MOD_SHIFT;
        if (kc.keyboardModifiers() & Qt::AltModifier) mods |= MOD_ALT;
        if (kc.keyboardModifiers() & Qt::MetaModifier) mods |= MOD_WIN;
        const int id = m_nextId++;
        if (vk && RegisterHotKey(nullptr, id, mods, vk)) m_nativeIds.insert(id, a.id);
        else { m_failed << a.id; qWarning("Hotkey %s for %s could not be registered", qPrintable(seq.toString()), qPrintable(a.id)); }
#else
        if (!m_host) continue;
        auto *sc = new QShortcut(seq, m_host);
        sc->setContext(Qt::ApplicationShortcut);
        const QString id = a.id;
        connect(sc, &QShortcut::activated, this, [this, id] {
            for (const auto &x : m_actions) if (x.id == id && x.fn) x.fn();
            Q_EMIT triggered(id);
        });
        m_shortcuts.insert(a.id, sc);
#endif
    }
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) {
#ifdef _WIN32
    if (eventType != "windows_generic_MSG") return false;
    const MSG *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY) return false;
    const QString id = m_nativeIds.value(int(msg->wParam));
    if (id.isEmpty()) return false;
    for (const auto &a : m_actions) if (a.id == id && a.fn) a.fn();
    Q_EMIT triggered(id);
    return true;
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
    return false;
#endif
}

} // namespace bc
