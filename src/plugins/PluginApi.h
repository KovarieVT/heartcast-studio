#pragma once
// ============================================================================
//  KovaLive! — App Plugin API v1
//  Build a Qt plugin (Q_PLUGIN_METADATA with this IID) as a DLL and ship it with
//  a plugin.json manifest. See docs/PLUGIN-API.md.
//
//  For new SOURCES / FILTERS / TRANSITIONS / ENCODERS / OUTPUTS write a standard
//  libobs module instead — KovaLive loads those natively (plugins/obs/…).
// ============================================================================
#include <QIcon>
#include <QJsonObject>
#include <QString>
#include <QtPlugin>
#include <functional>

class QWidget;

namespace bc {

// Services the host exposes to plugins. Each call is gated by the permissions
// the user approved for that plugin ("scenes", "streaming", "chat", "network").
class HostApi {
public:
    virtual ~HostApi() = default;
    virtual int apiVersion() const = 0;
    virtual QStringList sceneNames() const = 0;                  // scenes
    virtual QString currentScene() const = 0;                    // scenes
    virtual bool switchScene(const QString &name) = 0;           // scenes
    virtual bool isStreaming() const = 0;                        // streaming
    virtual void startStreaming() = 0;                           // streaming
    virtual void stopStreaming() = 0;                            // streaming
    virtual void sendChat(const QString &platform, const QString &text) = 0;   // chat
    virtual void onChat(std::function<void(const QString &platform, const QString &user, const QString &text)> cb) = 0; // chat
    virtual void onEvent(std::function<void(const QString &type, const QString &user, double amount)> cb) = 0;
    virtual void addDock(const QString &title, QWidget *widget) = 0;
    virtual void addHotkeyAction(const QString &id, const QString &label, std::function<void()> fn) = 0;
    virtual void log(const QString &message) = 0;
};

class KovaLivePlugin {
public:
    virtual ~KovaLivePlugin() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual bool load(HostApi *host) = 0;     // return false to abort loading
    virtual void unload() = 0;
};

} // namespace bc

#define KovaLivePlugin_iid "studio.kovalive.Plugin/1.0"
Q_DECLARE_INTERFACE(bc::KovaLivePlugin, KovaLivePlugin_iid)
