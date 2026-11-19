// Engine integration test — exercises the REAL libobs pipeline end to end:
// startup, module loading, starter scenes, sources, recording to disk,
// replay buffer, and multistreaming one shared encode to two RTMP servers.
// Run with tests/run_engine_test.sh (starts Xvfb + two local RTMP listeners).
#include <QtTest>
#include <QTemporaryDir>

#include "auth/SecureStore.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "engine/WidgetSource.h"
#include "overlays/WidgetManager.h"
#include "platforms/PlatformHub.h"

using namespace bc;

class EngineTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        Q_INIT_RESOURCE(resources);
        QVERIFY(m_tmp.isValid());
        qputenv("APPDATA", m_tmp.path().toUtf8());
        auto &s = Settings::instance();
        s.load(m_tmp.filePath("config.json"));
        s.set("video.baseWidth", 1280); s.set("video.baseHeight", 720);
        s.set("video.outWidth", 1280); s.set("video.outHeight", 720);
        s.set("video.fpsNum", 30);
        s.set("output.encoder", "obs_x264");
        s.set("output.bitrate", 2500);
        s.set("output.preset", "performance");
        s.set("recording.path", m_tmp.filePath("rec"));
        s.set("recording.remuxToMp4", false);
        s.set("recording.replayBufferSec", 10);
        Logger::installObsLogHandler();
        QStringList steps;
        QVERIFY2(Engine::instance().startup([&](int, const QString &m) { steps << m; }), "libobs startup failed");
        QVERIFY(steps.size() >= 5);
        PlatformHub::instance().init();
        WidgetManager::instance().init();
    }

    void starterScenesLoaded() {
        auto &e = Engine::instance();
        QVERIFY(e.sceneOrder().size() >= 5);
        QVERIFY(!e.programScene().isEmpty());
        QVERIFY(e.scene(e.programScene()) != nullptr);
    }

    void encodersAndSourceTypesDiscovered() {
        auto &e = Engine::instance();
        QVERIFY(!e.videoEncoders().isEmpty());
        QVERIFY(!e.bestEncoder().isEmpty());
        bool hasImage = false, hasGradient = false;
        for (const auto &t : e.sourceTypes()) { hasImage |= t.id == "image_source"; hasGradient |= t.id == "bc_gradient"; }
        QVERIFY(hasImage && hasGradient);
        QVERIFY(e.transitionTypes().contains("fade_transition"));
    }

    void sceneCrudAndPersistence() {
        auto &e = Engine::instance();
        const QString n = e.createScene("Test Scene");
        QVERIFY(e.scene(n));
        OBSDataAutoRelease g = obs_data_create();
        obs_data_set_string(g, "bc_c1", "#F5A6C8");
        obs_data_set_string(g, "bc_c2", "#A9E5D0");
        QVERIFY(e.addSource(n, "bc_gradient", "Grad", g));
        OBSDataAutoRelease w = obs_data_create();
        obs_data_set_string(w, "kind", "goal");
        QVERIFY(e.addSource(n, "bc_widget", "Goal Widget", w));
        QTRY_VERIFY_WITH_TIMEOUT(!WidgetRegistry::instance().sources().isEmpty(), 3000);
        const QString dup = e.duplicateScene(n);
        QVERIFY(e.scene(dup));
        QVERIFY(e.renameScene(dup, "Renamed ♡"));
        e.transitionTo(n);
        QCOMPARE(e.programScene(), n);

        const QString path = m_tmp.filePath("collection.json");
        QVERIFY(e.saveCollection(path));
        const int count = int(e.sceneOrder().size());
        QVERIFY(e.loadCollection(path));
        QCOMPARE(int(e.sceneOrder().size()), count);
        QVERIFY(e.sceneOrder().contains("Renamed ♡"));
        QVERIFY(e.removeScene("Renamed ♡"));
        e.transitionTo(n);
    }

    void recordingProducesAFile() {
        auto &e = Engine::instance();
        QSignalSpy spy(&e, &Engine::recordingStateChanged);
        QString err;
        QVERIFY2(e.startRecording(&err), qPrintable(err));
        QTRY_VERIFY_WITH_TIMEOUT(e.isRecording(), 3000);
        QTest::qWait(1500);
        QVERIFY(e.pauseRecording(true));
        QVERIFY(e.isRecordingPaused());
        QTest::qWait(500);
        QVERIFY(e.pauseRecording(false));
        QTest::qWait(1500);
        e.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!e.isRecording(), 8000);
        const QFileInfo fi(e.lastRecordingPath());
        QVERIFY2(fi.exists(), qPrintable(fi.filePath()));
        QVERIFY2(fi.size() > 20000, qPrintable(QString::number(fi.size())));
        qInfo("Recorded %s (%lld bytes)", qPrintable(fi.fileName()), fi.size());
    }

    void replayBufferSaves() {
        auto &e = Engine::instance();
        QSignalSpy saved(&e, &Engine::replaySaved);
        QString err;
        QVERIFY2(e.startReplayBuffer(&err), qPrintable(err));
        QTRY_VERIFY_WITH_TIMEOUT(e.isReplayBufferActive(), 3000);
        QTest::qWait(3000);
        QVERIFY(e.saveReplay());
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, 8000);
        const QString path = saved.first().first().toString();
        QVERIFY2(QFileInfo(path).size() > 10000, qPrintable(path));
        e.stopReplayBuffer();
        QTRY_VERIFY_WITH_TIMEOUT(!e.isReplayBufferActive(), 5000);
    }

    void multistreamTwoDestinationsSharedEncoder() {
        if (qEnvironmentVariableIsEmpty("HC_RTMP_A")) QSKIP("Set HC_RTMP_A / HC_RTMP_B (run_engine_test.sh does this)");
        auto &e = Engine::instance();
        QVector<Destination> dests;
        for (const auto &[envName, id] : {std::pair{"HC_RTMP_A", "a"}, std::pair{"HC_RTMP_B", "b"}}) {
            Destination d;
            d.id = id; d.platform = "custom"; d.name = QString("Local %1").arg(id);
            d.server = qEnvironmentVariable(envName);
            d.keyRef = QString("streamkey.test.%1").arg(id);
            QVERIFY(SecureStore::put(d.keyRef, QByteArray("key") + id));
            QCOMPARE(SecureStore::get(d.keyRef), QByteArray("key") + id);
            dests << d;
        }
        QString err;
        QVERIFY2(e.startStreaming(dests, &err), qPrintable(err));
        QTRY_VERIFY_WITH_TIMEOUT(e.destinationStatus("a").state == OutputState::Live &&
                                 e.destinationStatus("b").state == OutputState::Live, 10000);
        QTest::qWait(1200); e.pollOutputStats();
        QTest::qWait(1200); e.pollOutputStats();
        QVERIFY(e.destinationStatus("a").kbps > 100);
        QVERIFY(e.destinationStatus("b").kbps > 100);
        qInfo("Multistream live: A %.0f kbps, B %.0f kbps", e.destinationStatus("a").kbps, e.destinationStatus("b").kbps);
        QTest::qWait(2000);
        e.stopDestination("b");                       // stop one, the other keeps going
        QTRY_VERIFY_WITH_TIMEOUT(e.destinationStatus("b").state == OutputState::Off, 8000);
        QVERIFY(e.destinationStatus("a").state == OutputState::Live);
        e.stopStreaming();
        QTRY_VERIFY_WITH_TIMEOUT(!e.isStreaming(), 8000);
    }

    void badServerReportsReadableError() {
        auto &e = Engine::instance();
        Destination d; d.id = "bad"; d.name = "Nowhere"; d.server = "rtmp://127.0.0.1:1/live"; d.keyRef = "streamkey.test.bad";
        SecureStore::put(d.keyRef, "x");
        QString err;
        e.startStreaming({d}, &err);
        QTRY_VERIFY_WITH_TIMEOUT(e.destinationStatus("bad").state == OutputState::Error, 15000);
        QVERIFY(!e.destinationStatus("bad").error.isEmpty());
        qInfo("Error shown to user: %s", qPrintable(e.destinationStatus("bad").error));
    }

    void missingKeyIsRejectedUpFront() {
        Destination d; d.id = "nokey"; d.name = "No key"; d.server = "rtmp://127.0.0.1/live"; d.keyRef = "streamkey.none";
        QString err;
        QVERIFY(!Engine::instance().startStreaming({d}, &err));
        QVERIFY(err.contains("missing"));
    }

    void virtualCamFailsHonestlyWithoutDriver() {
        auto &e = Engine::instance();
        QString err;
        if (e.startVirtualCam(&err)) { e.stopVirtualCam(); QSKIP("A virtual camera driver is installed here"); }
        QVERIFY(!err.isEmpty());
        qInfo("Virtual camera message: %s", qPrintable(err));
    }

    void cleanupTestCase() {
        WidgetManager::instance().shutdown();
        Engine::instance().shutdown();
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_MAIN(EngineTest)
#include "test_engine.moc"
