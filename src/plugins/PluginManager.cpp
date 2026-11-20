#include "plugins/PluginManager.h"
#include "core/HotkeyManager.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>

#ifdef _WIN32
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#endif

namespace bc {

PluginManager &PluginManager::instance() {
    static PluginManager m;
    return m;
}

QString PluginManager::authenticodeSigner(const QString &file) {
#ifdef _WIN32
    const std::wstring path = file.toStdWString();
    WINTRUST_FILE_INFO fi{sizeof(fi)};
    fi.pcwszFilePath = path.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA wd{sizeof(wd)};
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    const LONG st = WinVerifyTrust(nullptr, &action, &wd);
    QString signer;
    if (st == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA *pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR *sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT *cert = sg ? WTHelperGetProvCertFromChain(sg, 0) : nullptr;
        if (cert) {
            wchar_t name[256] = {};
            CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
            signer = QString::fromWCharArray(name);
        }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &wd);
    return signer;
#else
    Q_UNUSED(file);
    return {};
#endif
}

static QString sha256Of(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QList<PluginEntry> PluginManager::scan() {
    QList<PluginEntry> found;
    const QStringList enabled = Settings::instance().get("plugins.enabled").toStringList();
    QDir root(Paths::plugins());
    for (const QFileInfo &dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile mf(dir.absoluteFilePath() + "/plugin.json");
        if (!mf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject m = QJsonDocument::fromJson(mf.readAll()).object();
        PluginEntry e;
        e.id = m.value("id").toString(dir.fileName());
        e.name = m.value("name").toString(e.id);
        e.version = m.value("version").toString("0.0.0");
        e.author = m.value("author").toString();
        e.description = m.value("description").toString();
        e.kind = m.value("kind").toString("app");
        e.apiVersion = m.value("apiVersion").toInt(1);
        for (auto p : m.value("permissions").toArray()) e.permissions << p.toString();
        e.path = dir.absoluteFilePath() + "/" + m.value("library").toString();
        e.compatible = e.apiVersion == apiVersion();
        if (!QFile::exists(e.path)) { e.error = tr("Library file missing"); e.compatible = false; }
        else {
            e.hashOk = sha256Of(e.path).compare(m.value("sha256").toString(), Qt::CaseInsensitive) == 0;
            e.signer = authenticodeSigner(e.path);
            if (!e.hashOk) e.error = tr("SHA-256 does not match plugin.json — file may be corrupted or tampered with.");
        }
        if (!e.compatible && e.error.isEmpty()) e.error = tr("Built for plugin API v%1; this KovaLive supports v%2.").arg(e.apiVersion).arg(apiVersion());
        e.enabled = enabled.contains(e.id);
        e.loaded = m_loaders.contains(e.id) && m_loaders[e.id]->isLoaded();
        found << e;
    }
    m_entries = found;
    Q_EMIT pluginsChanged();
    return found;
}

bool PluginManager::setEnabled(const QString &id, bool on, QString *error) {
    QStringList enabled = Settings::instance().get("plugins.enabled").toStringList();
    enabled.removeAll(id);
    if (on) {
        for (auto &e : m_entries) {
            if (e.id != id) continue;
            if (!e.compatible || !e.hashOk) { if (error) *error = e.error; return false; }
            enabled << id;
            Settings::instance().set("plugins.enabled", enabled);
            Settings::instance().save();
            if (e.kind == "app" && !loadOne(e)) { if (error) *error = e.error; return false; }
            if (e.kind == "obs-module" && error) *error = tr("Restart KovaLive to load this engine module.");
        }
    } else {
        if (auto *l = m_loaders.take(id)) {
            if (auto *p = qobject_cast<KovaLivePlugin *>(l->instance())) p->unload();
            l->unload();
            delete l;
        }
        Settings::instance().set("plugins.enabled", enabled);
        Settings::instance().save();
    }
    scan();
    return true;
}

bool PluginManager::loadOne(PluginEntry &e) {
    if (m_loaders.contains(e.id)) return true;
    auto *loader = new QPluginLoader(e.path, this);
    QObject *inst = loader->instance();
    auto *plugin = qobject_cast<KovaLivePlugin *>(inst);
    if (!plugin) {
        e.error = tr("Not a KovaLive plugin: %1").arg(loader->errorString());
        qWarning("Plugin %s failed: %s", qPrintable(e.id), qPrintable(e.error));
        loader->unload();
        delete loader;
        return false;
    }
    m_current = e.id;
    bool ok = false;
    try { ok = plugin->load(this); }
    catch (...) { ok = false; e.error = tr("Plugin threw an exception during load."); }
    m_current.clear();
    if (!ok) { loader->unload(); delete loader; if (e.error.isEmpty()) e.error = tr("Plugin refused to load."); return false; }
    m_loaders.insert(e.id, loader);
    e.loaded = true;
    qInfo("Plugin loaded: %s %s", qPrintable(e.id), qPrintable(e.version));
    return true;
}

void PluginManager::loadEnabled() {
    scan();
    for (auto &e : m_entries)
        if (e.enabled && e.kind == "app" && e.compatible && e.hashOk) loadOne(e);
    Q_EMIT pluginsChanged();
}

void PluginManager::unloadAll() {
    for (auto *l : std::as_const(m_loaders)) {
        if (auto *p = qobject_cast<KovaLivePlugin *>(l->instance())) p->unload();
        l->unload();
        delete l;
    }
    m_loaders.clear();
}

bool PluginManager::permitted(const QString &perm) const {
    // The plugin making the call is identified during load(); afterwards callbacks
    // were registered under that plugin's approved permissions.
    for (const auto &e : m_entries) if (e.id == m_current || m_current.isEmpty()) if (e.permissions.contains(perm)) return true;
    qWarning("Plugin call blocked: permission \"%s\" not granted", qPrintable(perm));
    return false;
}

QStringList PluginManager::sceneNames() const { return permitted("scenes") ? Engine::instance().sceneOrder() : QStringList(); }
QString PluginManager::currentScene() const { return permitted("scenes") ? Engine::instance().programScene() : QString(); }
bool PluginManager::switchScene(const QString &n) {
    if (!permitted("scenes") || !Engine::instance().sceneSource(n)) return false;
    Engine::instance().setPreviewScene(n);
    if (Engine::instance().studioMode()) Engine::instance().transitionTo(n);
    return true;
}
bool PluginManager::isStreaming() const { return Engine::instance().isStreaming(); }
void PluginManager::startStreaming() { if (permitted("streaming")) Q_EMIT startStreamingRequested(); }
void PluginManager::stopStreaming() { if (permitted("streaming")) Engine::instance().stopStreaming(); }
void PluginManager::sendChat(const QString &p, const QString &t) { if (permitted("chat")) PlatformHub::instance().sendChat(p, t); }
void PluginManager::onChat(std::function<void(const QString &, const QString &, const QString &)> cb) {
    if (!permitted("chat")) return;
    connect(&PlatformHub::instance(), &PlatformHub::chatMessage, this, [cb](const ChatMessage &m) { if (!m.hidden) cb(m.platform, m.user, m.text); });
}
void PluginManager::onEvent(std::function<void(const QString &, const QString &, double)> cb) {
    connect(&PlatformHub::instance(), &PlatformHub::streamEvent, this, [cb](const StreamEvent &e) { cb(e.type, e.user, e.amount); });
}
void PluginManager::addDock(const QString &title, QWidget *widget) {
    if (!m_main || !widget) return;
    auto *dock = new QDockWidget(title, m_main);
    dock->setObjectName("plugin-dock-" + title);
    dock->setWidget(widget);
    m_main->addDockWidget(Qt::RightDockWidgetArea, dock);
}
void PluginManager::addHotkeyAction(const QString &id, const QString &label, std::function<void()> fn) {
    HotkeyManager::instance().registerAction("plugin." + id, label, tr("Plugins"), std::move(fn));
}
void PluginManager::log(const QString &message) { qInfo("[plugin] %s", qPrintable(message)); }

} // namespace bc
