#include "core/Settings.h"
#include "core/Paths.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace bc {

Settings &Settings::instance() {
    static Settings s;
    return s;
}

QJsonObject Settings::defaults() {
    return QJsonObject{
        {"version", 1},
        {"general", QJsonObject{
            {"firstRunComplete", false},
            {"setupMode", "beginner"},
            {"language", "en"},
            {"startupAnimations", true},
            {"reduceEffectsWhileLive", true},
            {"notifications", true},
            {"autosaveMinutes", 2},
            {"confirmStopStream", true},
            {"checkUpdates", true},
            {"crashReports", "local"}   // local | off  (no remote upload without consent)
        }},
        {"appearance", QJsonObject{
            {"theme", "nocturne"},
            {"accent", "#9E1B32"},
            {"uiScale", 1.0},
            {"fontPt", 10},
            {"compact", false},
            {"ornaments", true}
        }},
        {"video", QJsonObject{
            {"baseWidth", 1920}, {"baseHeight", 1080},
            {"outWidth", 1920},  {"outHeight", 1080},
            {"fpsNum", 60}, {"fpsDen", 1},
            {"colorSpace", "709"}, {"colorRange", "partial"},
            {"scaleType", "bicubic"}
        }},
        {"audio", QJsonObject{
            {"sampleRate", 48000},
            {"channels", "stereo"},
            {"desktopDevice", "default"},
            {"micDevice", "default"},
            {"monitorDevice", "default"}
        }},
        {"output", QJsonObject{
            {"encoder", "auto"},          // auto | obs_x264 | jim_nvenc | h264_texture_amf | obs_qsv11 | av1/hevc variants
            {"rateControl", "CBR"},
            {"bitrate", 6000},
            {"keyintSec", 2},
            {"preset", "balanced"},
            {"profile", "high"},
            {"bframes", 2},
            {"audioCodec", "aac"},
            {"audioBitrate", 160},
            {"reconnectRetries", 20},
            {"reconnectDelaySec", 3},
            {"performanceProfile", "balanced"}
        }},
        {"recording", QJsonObject{
            {"path", Paths::defaultRecordings()},
            {"format", "mkv"},
            {"quality", "stream"},        // stream | high | indistinguishable | lossless
            {"tracks", 1},
            {"autoRecordWhenLive", false},
            {"remuxToMp4", true},
            {"replayBufferSec", 30},
            {"replayBufferEnabled", false}
        }},
        {"destinations", QJsonArray{}},  // [{id, platform, name, server, enabled, separateEncoder, bitrate}]
        {"hotkeys", QJsonObject{}},
        {"plugins", QJsonObject{{"enabled", QJsonArray{}}, {"allowUnsigned", false}}},
        {"layout", QJsonObject{}},
        {"advanced", QJsonObject{
            {"logLevel", "info"},
            {"processPriority", "above_normal"},
            {"gpuIndex", 0}
        }}
    };
}

static QJsonObject mergeDefaults(const QJsonObject &def, const QJsonObject &user) {
    QJsonObject out = def;
    for (auto it = user.begin(); it != user.end(); ++it) {
        if (it->isObject() && def.value(it.key()).isObject())
            out[it.key()] = mergeDefaults(def.value(it.key()).toObject(), it->toObject());
        else
            out[it.key()] = *it;
    }
    return out;
}

bool Settings::load(const QString &path) {
    m_path = path;
    QFile f(path);
    QJsonObject user;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            user = doc.object();
        } else {
            // Corrupt config: keep a backup and fall back to defaults instead of crashing.
            f.close();
            QFile::remove(path + ".corrupt");
            QFile::copy(path, path + ".corrupt");
            qWarning("config.json was corrupt (%s); backed up and reset", qPrintable(err.errorString()));
        }
    }
    m_root = mergeDefaults(defaults(), user);
    return true;
}

bool Settings::save() const {
    if (m_path.isEmpty()) return false;
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool Settings::exportTo(const QString &path) const {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool Settings::importFrom(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = err.errorString();
        return false;
    }
    m_root = mergeDefaults(defaults(), doc.object());
    save();
    Q_EMIT changed(QString());
    return true;
}

void Settings::resetToDefaults() {
    m_root = defaults();
    save();
    Q_EMIT changed(QString());
}

QVariant Settings::get(const QString &key, const QVariant &fallback) const {
    const QStringList parts = key.split('.');
    QJsonValue v = m_root;
    for (const auto &p : parts) {
        if (!v.isObject()) return fallback;
        v = v.toObject().value(p);
        if (v.isUndefined()) return fallback;
    }
    return v.toVariant();
}

static void setPath(QJsonObject &obj, QStringList parts, const QJsonValue &val) {
    const QString head = parts.takeFirst();
    if (parts.isEmpty()) { obj[head] = val; return; }
    QJsonObject child = obj.value(head).toObject();
    setPath(child, parts, val);
    obj[head] = child;
}

void Settings::set(const QString &key, const QVariant &value) {
    setPath(m_root, key.split('.'), QJsonValue::fromVariant(value));
    Q_EMIT changed(key);
}

} // namespace bc
