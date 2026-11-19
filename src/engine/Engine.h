#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>
#include <obs.hpp>

// libobs handles are opaque C structs; let Qt pass them through signals.
Q_DECLARE_OPAQUE_POINTER(obs_source_t *)
Q_DECLARE_OPAQUE_POINTER(obs_sceneitem_t *)
Q_DECLARE_OPAQUE_POINTER(obs_scene_t *)

namespace bc {

struct SourceTypeInfo {
    QString id;
    QString name;
    QString category;   // capture | media | audio | creative | widget
    bool video = false, audio = false;
};

struct EncoderInfo {
    QString id;
    QString name;
    QString codec;      // h264 | hevc | av1 | aac | opus
    QString family;     // x264 | nvenc | amf | qsv | other
    bool hardware = false;
};

// One streaming destination (one RTMP connection). Several destinations may
// share one encoder pair, so N platforms cost one encode (unless the user asks
// for a destination-specific bitrate, which gets its own encoder).
struct Destination {
    QString id;          // stable uuid
    QString platform;    // twitch | youtube | kick | ... | custom
    QString name;
    QString server;
    QString keyRef;      // SecureStore name holding the stream key
    bool    enabled = true;
    bool    separateEncoder = false;
    int     bitrate = 0; // only used with separateEncoder
    QJsonObject toJson() const;
    static Destination fromJson(const QJsonObject &o);
};

enum class OutputState { Off, Starting, Live, Reconnecting, Stopping, Error };

struct DestinationStatus {
    OutputState state = OutputState::Off;
    QString     error;
    double      kbps = 0;
    int         droppedFrames = 0;
    int         totalFrames = 0;
    qint64      startedMs = 0;
};

struct EngineStats {
    double activeFps = 0;
    int    renderLagged = 0, renderTotal = 0;
    int    encoderSkipped = 0, encoderTotal = 0;
    double avgFrameMs = 0;
};

class Engine : public QObject {
    Q_OBJECT
public:
    static Engine &instance();

    // ---- lifecycle ----
    bool startup(const std::function<void(int pct, const QString &msg)> &progress);
    void shutdown();
    bool isRunning() const { return m_running; }
    bool resetVideo(QString *error = nullptr);
    bool resetAudio();

    // ---- discovery ----
    QVector<SourceTypeInfo> sourceTypes() const;
    QVector<SourceTypeInfo> filterTypes(bool audio) const;
    QVector<EncoderInfo>    videoEncoders() const;
    QString bestEncoder(const QString &codec = "h264") const;
    QStringList transitionTypes() const;

    // ---- scenes ----
    QStringList sceneOrder() const { return m_sceneOrder; }
    obs_source_t *sceneSource(const QString &name) const;
    obs_scene_t  *scene(const QString &name) const;
    QString createScene(const QString &name);
    bool    removeScene(const QString &name);
    bool    renameScene(const QString &from, const QString &to);
    QString duplicateScene(const QString &name);
    void    moveScene(const QString &name, int newIndex);

    QString programScene() const { return m_programScene; }
    QString previewScene() const { return m_previewScene; }
    void    setPreviewScene(const QString &name);
    void    transitionTo(const QString &name);   // program switch via current transition
    bool    studioMode() const { return m_studioMode; }
    void    setStudioMode(bool on);

    // ---- sources / scene items ----
    obs_sceneitem_t *addSource(const QString &sceneName, const QString &typeId,
                               const QString &name, obs_data_t *settings = nullptr);
    obs_sceneitem_t *addExistingSource(const QString &sceneName, obs_source_t *src);
    QString uniqueSourceName(const QString &base) const;
    void    fitItemToCanvas(obs_sceneitem_t *item, double scale = 1.0, const QString &align = "center");

    // ---- transitions ----
    obs_source_t *transition() const { return m_transition; }
    QString transitionId() const;
    void    setTransition(const QString &typeId, obs_data_t *settings = nullptr);
    int     transitionDurationMs() const { return m_transitionMs; }
    void    setTransitionDurationMs(int ms) { m_transitionMs = ms; }

    // ---- global audio ----
    obs_source_t *desktopAudio() const { return m_desktopAudio; }
    obs_source_t *micAudio() const { return m_micAudio; }
    QVector<QPair<QString, QString>> audioDevices(bool input) const;   // (id, name)
    void setAudioDevice(bool input, const QString &deviceId);
    void setMonitoringDevice(const QString &name, const QString &id);

    // ---- streaming ----
    bool startStreaming(const QVector<Destination> &dests, QString *error = nullptr);
    void stopStreaming();
    void stopDestination(const QString &destId);
    bool isStreaming() const;
    DestinationStatus destinationStatus(const QString &destId) const;
    QStringList activeDestinations() const { return m_streams.keys(); }

    // ---- recording ----
    bool startRecording(QString *error = nullptr);
    void stopRecording();
    bool pauseRecording(bool pause);
    bool isRecording() const;
    bool isRecordingPaused() const;
    QString lastRecordingPath() const { return m_lastRecording; }

    bool startReplayBuffer(QString *error = nullptr);
    void stopReplayBuffer();
    bool saveReplay();
    bool isReplayBufferActive() const;

    bool startVirtualCam(QString *error = nullptr);
    void stopVirtualCam();
    bool isVirtualCamActive() const;
    bool virtualCamAvailable() const;

    EngineStats stats() const;
    void pollOutputStats();    // call ~1/s from the UI thread; updates kbps/dropped

    // Remuxes MKV → MP4 in a worker thread (libobs media-remux; no re-encode).
    void remuxToMp4(const QString &mkvPath);

    // ---- persistence ----
    bool saveCollection(const QString &path) const;
    bool loadCollection(const QString &path);
    bool loadStarterCollection();
    QString currentCollectionPath() const { return m_collectionPath; }

    // Generates a gradient PNG into the cache (backs the "Gradient" source type).
    static QString makeGradientImage(const QString &c1, const QString &c2, int w, int h, int angleDeg = 135);
    static QString kawaiiLumaDir();

Q_SIGNALS:
    void scenesChanged();
    void sceneItemsChanged(const QString &sceneName);
    void programSceneChanged(const QString &name);
    void previewSceneChanged(const QString &name);
    void streamingStateChanged();
    void destinationStateChanged(const QString &destId);
    void recordingStateChanged();
    void replayBufferStateChanged();
    void replaySaved(const QString &path);
    void virtualCamStateChanged();
    void engineError(const QString &message);

private:
    struct OutHolder;
    Engine() = default;
    obs_encoder_t *makeVideoEncoder(const QString &name, int bitrateOverride);
    obs_encoder_t *makeAudioEncoder(const QString &name, size_t mixIdx);
    void connectOutputSignals(OutHolder &h, const QString &tag);
    void onOutputSignal(const QString &tag, const QString &what, int code);
    void releaseStreamEncodersIfIdle();
    void createGlobalAudio();
    void generateKawaiiLumas();
    void clearAll();
    QString uniqueSceneName(const QString &base) const;

    // Signals are declared after the output so they disconnect BEFORE the
    // output (and its signal handler) is released.
    struct OutHolder {
        OBSOutputAutoRelease   out;
        std::vector<OBSSignal> sigs;
        void reset() { sigs.clear(); out = nullptr; }
    };
    struct StreamSlot {
        OBSServiceAutoRelease service;
        OBSEncoderAutoRelease ownVideoEnc;   // only if separateEncoder
        OutHolder             o;
        DestinationStatus     status;
        quint64 lastBytes = 0; qint64 lastBytesMs = 0;
        QString name;
    };

    bool m_running = false;
    bool m_studioMode = false;
    int  m_transitionMs = 450;
    QStringList m_sceneOrder;
    QString m_programScene, m_previewScene, m_collectionPath, m_lastRecording;

    OBSSourceAutoRelease  m_transition;
    OBSSourceAutoRelease  m_desktopAudio, m_micAudio;
    OBSEncoderAutoRelease m_streamVideoEnc, m_streamAudioEnc;
    OBSEncoderAutoRelease m_recVideoEnc, m_recAudioEnc;
    OutHolder m_record, m_replay, m_vcam;
    QHash<QString, StreamSlot *> m_streams;
    std::vector<OBSSignal> m_sceneSignals;
};

QString outputStateName(OutputState s);

} // namespace bc
