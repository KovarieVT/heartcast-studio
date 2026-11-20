#pragma once
#include "plugins/PluginApi.h"
#include <QObject>
#include <QPluginLoader>
#include <QPointer>

#include <QMainWindow>

namespace bc {

struct PluginEntry {
    QString id, name, version, author, description, path, kind;   // kind: app | obs-module
    QStringList permissions;
    int apiVersion = 1;
    bool enabled = false, loaded = false, compatible = true, hashOk = false;
    QString signer;      // Authenticode signer, empty if unsigned
    QString error;
};

// Discovers plugins in %APPDATA%/KovaLive/plugins/<id>/ (each with plugin.json),
// verifies SHA-256 against the manifest and the Authenticode signature (Windows),
// and loads only plugins the user explicitly enabled after seeing permissions.
// NOTE: native plugins run in-process — permissions gate the HostApi surface, they
// are not an OS sandbox. This is stated in the UI and docs.
class PluginManager : public QObject, public HostApi {
    Q_OBJECT
public:
    static PluginManager &instance();
    void setMainWindow(QMainWindow *w) { m_main = w; }

    QList<PluginEntry> scan();
    QList<PluginEntry> entries() const { return m_entries; }
    bool setEnabled(const QString &id, bool on, QString *error = nullptr);
    void loadEnabled();
    void unloadAll();

    // HostApi
    int apiVersion() const override { return 1; }
    QStringList sceneNames() const override;
    QString currentScene() const override;
    bool switchScene(const QString &name) override;
    bool isStreaming() const override;
    void startStreaming() override;
    void stopStreaming() override;
    void sendChat(const QString &platform, const QString &text) override;
    void onChat(std::function<void(const QString &, const QString &, const QString &)> cb) override;
    void onEvent(std::function<void(const QString &, const QString &, double)> cb) override;
    void addDock(const QString &title, QWidget *widget) override;
    void addHotkeyAction(const QString &id, const QString &label, std::function<void()> fn) override;
    void log(const QString &message) override;

    static QString authenticodeSigner(const QString &file);

Q_SIGNALS:
    void pluginsChanged();
    void startStreamingRequested();

private:
    PluginManager() = default;
    bool loadOne(PluginEntry &e);
    bool permitted(const QString &perm) const;
    QList<PluginEntry> m_entries;
    QHash<QString, QPluginLoader *> m_loaders;
    QString m_current;   // plugin currently being called (for permission checks during load)
    QPointer<QMainWindow> m_main;
};

} // namespace bc
