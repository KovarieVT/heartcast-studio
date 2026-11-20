#pragma once
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace bc {

// One entry in the Widget Gallery. Several entries can share a renderer `kind` with
// different defaults (e.g. "Latest follower" and "Latest subscriber" are both labels).
struct WidgetCatalogEntry {
    QString id;          // stable id, e.g. "label.latest_follower"
    QString name;        // "Latest follower"
    QString category;    // "Labels"
    QString description; // one line for the gallery
    QString kind;        // renderer, e.g. "label"
    QJsonObject defaults;// obs settings applied on creation (merged over the source defaults)
    QSize size;          // initial size on the canvas
};

const QVector<WidgetCatalogEntry> &widgetCatalog();
QStringList widgetCategories();                       // in display order
const WidgetCatalogEntry *widgetCatalogEntry(const QString &id);
QStringList widgetKinds();                            // every renderer kind (for the properties list)
QString widgetKindName(const QString &kind);          // human name for a kind

} // namespace bc
