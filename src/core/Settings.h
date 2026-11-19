#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

namespace bc {

// Hierarchical JSON settings with defaults, dotted keys ("video.baseWidth"),
// atomic saves (QSaveFile) and import/export. Secrets are NEVER stored here —
// see auth/SecureStore.
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    bool load(const QString &path);
    bool save() const;
    bool exportTo(const QString &path) const;
    bool importFrom(const QString &path, QString *error = nullptr);
    void resetToDefaults();

    QVariant get(const QString &key, const QVariant &fallback = {}) const;
    void set(const QString &key, const QVariant &value);

    int     i(const QString &k) const { return get(k).toInt(); }
    bool    b(const QString &k) const { return get(k).toBool(); }
    QString s(const QString &k) const { return get(k).toString(); }
    double  d(const QString &k) const { return get(k).toDouble(); }

    QJsonObject raw() const { return m_root; }
    static QJsonObject defaults();

Q_SIGNALS:
    void changed(const QString &key);

private:
    Settings() = default;
    QJsonObject m_root;
    QString m_path;
};

} // namespace bc
