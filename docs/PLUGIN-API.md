# Plugin development

HeartCast supports two kinds of plugins.

## 1. Engine plugins (libobs modules)
New **sources, filters, transitions, encoders, outputs and services** are standard libobs modules,
exactly like OBS plugins (`obs_register_source` and so on). The libobs plugin docs apply:
<https://docs.obsproject.com/plugins>.

Folder layout in `%APPDATA%\HeartCast\plugins\<id>\`:
```
plugin.json        { "id": "my-filter", "name": "My Filter", "version": "1.0.0", "kind": "obs-module",
                     "library": "bin/my-filter.dll", "sha256": "<hex of the dll>", "apiVersion": 1 }
bin/my-filter.dll
data/...           module data (effects, locale)
```
Engine modules load only after the user enables them on the Plugins page and restarts. OBS
*frontend* plugins (UI docks, obs-websocket, frontend-tools) are not supported, because they
depend on OBS's own window.

## 2. App plugins (Qt)
App plugins add **docks, automations, hotkey actions, chat bots and platform integrations**.
Implement `bc::HeartCastPlugin` from `src/plugins/PluginApi.h`:

```cpp
#include "plugins/PluginApi.h"
#include <QLabel>

class HelloPlugin : public QObject, public bc::HeartCastPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID HeartCastPlugin_iid)
    Q_INTERFACES(bc::HeartCastPlugin)
public:
    QString id() const override { return "hello"; }
    QString name() const override { return "Hello Panel"; }
    QString version() const override { return "1.0.0"; }
    bool load(bc::HostApi *host) override {
        host->addDock("Hello ♡", new QLabel("hi from a plugin!"));
        host->onEvent([host](const QString &type, const QString &user, double) {
            if (type == "follow") host->sendChat("all", "thank you " + user + " ♡");   // needs "chat"
        });
        host->addHotkeyAction("hello.next", "Next scene", [host] {
            const auto s = host->sceneNames();                                       // needs "scenes"
            if (!s.isEmpty()) host->switchScene(s.at((s.indexOf(host->currentScene()) + 1) % s.size()));
        });
        return true;
    }
    void unload() override {}
};
```
Build it as a shared library against the same Qt version as HeartCast. Then add `plugin.json`:
```json
{ "id": "hello", "name": "Hello Panel", "version": "1.0.0", "author": "you", "kind": "app",
  "apiVersion": 1, "library": "hello.dll", "sha256": "<sha256 of hello.dll>",
  "permissions": ["scenes", "chat"], "description": "Says hi." }
```

### Permissions
| Permission | Grants |
|---|---|
| `scenes` | `sceneNames`, `currentScene`, `switchScene` |
| `streaming` | `startStreaming`, `stopStreaming` |
| `chat` | `sendChat`, `onChat` |
| *(none)* | `onEvent`, `addDock`, `addHotkeyAction`, `log`, `isStreaming` |

### Safety model
* Every plugin is **disabled by default**. When enabling, the user sees the requested permissions
  and the Authenticode signer (or "unsigned").
* The SHA-256 in `plugin.json` must match the DLL, otherwise it won't load (catches corruption and
  tampering).
* API versions are checked. Load failures and exceptions are caught and logged.
* **Important:** plugins are native code running in-process. Permissions limit what the HeartCast
  API will do for a plugin, but they are **not a sandbox**. Only install plugins you trust.
