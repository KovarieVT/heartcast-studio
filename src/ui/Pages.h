#pragma once
#include "core/HardwareInfo.h"
#include "platforms/Platform.h"
#include <QDateTime>
#include <QWidget>

class QLabel;
class QListWidget;
class QTableWidget;
class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QVBoxLayout;
class QGridLayout;
class QTreeWidget;
class QSpinBox;
class QCheckBox;

namespace bc {

class SourcesPanel;

// Rolling per-second samples for the dashboard + analytics.
struct SessionHistory {
    static SessionHistory &instance();
    void sample();
    QList<double> cpu, gpu, kbps, fps;
    qint64 liveStartMs = 0;
    int peakViewers = 0, chatMessages = 0;
    UsageSampler usage;
    double lastCpu = 0, lastGpu = -1;
    quint64 lastRamMB = 0;
};

class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    void refresh();
Q_SIGNALS:
    void goLiveRequested();
    void openPage(int index);
private:
    QHash<QString, QLabel *> m_tiles;
    QLabel *m_live, *m_rec;
    QGridLayout *m_sceneGrid;
};

class StreamManagerPage : public QWidget {
    Q_OBJECT
public:
    explicit StreamManagerPage(QWidget *parent = nullptr);
    void reloadDestinations();
    void reloadAccounts();
    void refreshStatus();
    QString streamTitle() const;
Q_SIGNALS:
    void goLiveRequested();
private:
    void editDestination(const QString &id);
    void testNetwork();
    QTableWidget *m_dest;
    QVBoxLayout *m_accounts;
    QLineEdit *m_title, *m_category;
    QPlainTextEdit *m_desc;
    QComboBox *m_privacy;
    QLabel *m_bandwidth, *m_deviceCode;
};

class RecordingPage : public QWidget {
    Q_OBJECT
public:
    explicit RecordingPage(QWidget *parent = nullptr);
    void reloadLibrary();
    void refreshStatus();
private:
    QListWidget *m_lib;
    QLabel *m_status, *m_info;
};

class MediaLibraryPage : public QWidget {
    Q_OBJECT
public:
    MediaLibraryPage(QWidget *parent = nullptr);
    void reload();
    static void ensureStarterAssets();
protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    void importFiles(const QStringList &files);
    void addToScene(const QString &path);
    QStringList tagsFor(const QString &rel) const;
    void setTags(const QString &rel, const QStringList &tags);
    QListWidget *m_folders, *m_files;
    QLineEdit *m_search;
    QComboBox *m_filter;
    QLabel *m_preview, *m_meta;
};

class WidgetsPage : public QWidget {
    Q_OBJECT
public:
    WidgetsPage(SourcesPanel *sources, bool overlaysMode, QWidget *parent = nullptr);
private:
    void rebuildGallery();
    SourcesPanel *m_sources;
    QComboBox *m_pack;
    QGridLayout *m_grid;
    bool m_overlays;
};

class EffectsPage : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPage(QWidget *parent = nullptr);
    void reloadSources();
private:
    QComboBox *m_src;
    QLabel *m_aiStatus;
};

class TransitionsPage : public QWidget {
    Q_OBJECT
public:
    explicit TransitionsPage(QWidget *parent = nullptr);
private:
    void showCurrent();
    QListWidget *m_list;
    QWidget *m_propsHost;
    QSpinBox *m_duration;
};

class ChatPage : public QWidget {
    Q_OBJECT
public:
    explicit ChatPage(bool compact = false, QWidget *parent = nullptr);
private:
    void append(const ChatMessage &m);
    void contextMenu(const QPoint &pt);
    QListWidget *m_list;
    QLineEdit *m_input;
    QComboBox *m_target, *m_filterPlatform;
    QCheckBox *m_showHidden;
    QSpinBox *m_fontSize;
};

class ActivityPage : public QWidget {
    Q_OBJECT
public:
    explicit ActivityPage(bool compact = false, QWidget *parent = nullptr);
private:
    QListWidget *m_list;
};

class AnalyticsPage : public QWidget {
    Q_OBJECT
public:
    explicit AnalyticsPage(QWidget *parent = nullptr);
    void refresh();
private:
    QHash<QString, QLabel *> m_stats;
    QWidget *m_chart;
};

class PluginsPage : public QWidget {
    Q_OBJECT
public:
    explicit PluginsPage(QWidget *parent = nullptr);
    void reload();
private:
    QTableWidget *m_table;
    QListWidget *m_modules;
};

QString platformBadge(const QString &platformId);

} // namespace bc
