// Engine integration test — exercises the REAL libobs pipeline end to end:
// startup, module loading, starter scenes, sources, recording to disk,
// replay buffer, and multistreaming one shared encode to two RTMP servers.
// Run with tests/run_engine_test.sh (starts Xvfb + two local RTMP listeners).
#include <QtTest>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include "auth/SecureStore.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "engine/ShapeTransition.h"
#include "engine/WidgetSource.h"
#include "overlays/WidgetCatalog.h"
#include "overlays/WidgetManager.h"
#include "platforms/PlatformHub.h"

using namespace bc;

// Renders a source (e.g. the transition) the way the program output would, scaled to 320×180.
static QImage renderFrame(obs_source_t *src) {
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    const uint32_t w = 320, h = 180;
    QImage out;
    obs_enter_graphics();
    gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *ss = gs_stagesurface_create(w, h, GS_RGBA);
    if (gs_texrender_begin(tr, w, h)) {
        vec4 clear; vec4_zero(&clear);
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
        obs_source_video_render(src);
        gs_texrender_end(tr);
        gs_stage_texture(ss, gs_texrender_get_texture(tr));
        uint8_t *data = nullptr; uint32_t linesize = 0;
        if (gs_stagesurface_map(ss, &data, &linesize)) {
            out = QImage(int(w), int(h), QImage::Format_RGBA8888);
            for (uint32_t y = 0; y < h; ++y) memcpy(out.scanLine(int(y)), data + y * linesize, w * 4);
            gs_stagesurface_unmap(ss);
        }
    }
    gs_stagesurface_destroy(ss);
    gs_texrender_destroy(tr);
    obs_leave_graphics();
    return out;
}

static QString solidScene(const QString &name, uint32_t abgr) {
    auto &e = Engine::instance();
    const QString n = e.createScene(name);
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_int(st, "color", abgr);
    obs_data_set_int(st, "width", 1280);
    obs_data_set_int(st, "height", 720);
    e.addSource(n, "color_source_v3", name + " colour", st);
    return n;
}

static bool isRed(QRgb c) { return qRed(c) > 150 && qBlue(c) < 90; }
static bool isBlue(QRgb c) { return qBlue(c) > 150 && qRed(c) < 90; }

class EngineTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        Q_INIT_RESOURCE(resources);
        QVERIFY(m_tmp.isValid());
        qputenv("APPDATA", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_tmp.path().toUtf8());   // Linux: keep the test out of the real config
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

    // The gothic shape wipes are generated in memory (no image files), so they work from
    // a read-only Program Files install. Halfway through "Wing Sweep" the left side must
    // already show the new scene and the right side the old one.
    void shapeWipeRendersHalfway() {
        auto &e = Engine::instance();
        const QString a = solidScene("Wipe A", 0xFF0000FF), b = solidScene("Wipe B", 0xFFFF0000);   // red, blue (ABGR)
        QVERIFY(e.setTransition("kova_shape:wingsweep"));
        QCOMPARE(e.transitionId(), QStringLiteral("kova_shape:wingsweep"));
        obs_source_t *trs = e.transition();
        obs_source_t *sa = e.sceneSource(a), *sb = e.sceneSource(b);
        QVERIFY(sa && sb);
        obs_transition_set(trs, sa);
        QVERIFY(obs_transition_start(trs, OBS_TRANSITION_MODE_MANUAL, 0, sb));
        obs_transition_set_manual_time(trs, 0.5f);
        QTest::qWait(300);
        const QImage f = renderFrame(trs);
        QVERIFY(!f.isNull());
        const QRgb left = f.pixel(30, 90), right = f.pixel(290, 90);
        qInfo("wing sweep @50%%: left rgb(%d,%d,%d) right rgb(%d,%d,%d)", qRed(left), qGreen(left), qBlue(left), qRed(right), qGreen(right), qBlue(right));
        QVERIFY2(isBlue(left), "left half should already show the new scene");
        QVERIFY2(isRed(right), "right half should still show the old scene");
        obs_transition_set_manual_time(trs, 1.0f);
        QTest::qWait(200);
        obs_transition_force_stop(trs);
        obs_transition_set(trs, e.sceneSource(e.sceneOrder().first()));
        // every shape builds a full-range pattern
        for (const QString &shape : shapeTransitionShapes()) {
            const QImage pat = shapeTransitionPattern(shape, 160, 90);
            int lo = 255, hi = 0;
            for (int y = 0; y < pat.height(); ++y) for (int x = 0; x < pat.width(); ++x) { const int v = qGray(pat.pixel(x, y)); lo = qMin(lo, v); hi = qMax(hi, v); }
            QVERIFY2(lo <= 5 && hi >= 250, qPrintable(shape));
        }
        e.removeScene(a); e.removeScene(b);
    }

    // Every bundled stinger is a VP9 video with alpha. During the transition its overlay must,
    // at some moment, hide BOTH scenes everywhere (so the cut is invisible); afterwards the new
    // scene must be on air. A stinger that fails to decode would never cover anything.
    void stingersCoverTheCut() {
        auto &e = Engine::instance();
        const QStringList ids = Engine::stingerIds();
        QVERIFY2(ids.size() >= 20, qPrintable(QStringLiteral("only %1 stingers found").arg(ids.size())));
        const QString a = solidScene("Sting A", 0xFF0000FF), b = solidScene("Sting B", 0xFFFF0000);   // red, blue
        obs_source_t *sa = e.sceneSource(a), *sb = e.sceneSource(b);
        QVERIFY(sa && sb);
        auto covered = [](QRgb c) {   // neither the old (red) nor the new (blue) scene shows through
            const int dr = qAbs(qRed(c) - 255) + qGreen(c) + qBlue(c), db = qRed(c) + qGreen(c) + qAbs(qBlue(c) - 255);
            return dr > 60 && db > 60;
        };
        QStringList failures;
        for (const QString &id : ids) {
            QVERIFY(QFile::exists(Engine::stingerFile(id)));
            QVERIFY(e.setTransition("kova_stinger:" + id));
            obs_source_t *trs = e.transition();
            obs_transition_set(trs, sa);
            QTest::qWait(700);   // media source opens the file and learns its duration
            QElapsedTimer clock; clock.start();
            QVERIFY(obs_transition_start(trs, OBS_TRANSITION_MODE_AUTO, 300, sb));
            double best = 0;
            while (clock.elapsed() < 2300) {
                const QImage f = renderFrame(trs);
                int n = 0, hit = 0;
                for (int y = 2; y < f.height(); y += 6)
                    for (int x = 2; x < f.width(); x += 6) { ++n; hit += covered(f.pixel(x, y)); }
                best = qMax(best, hit / double(n));
                QTest::qWait(25);
            }
            const bool landed = isBlue(renderFrame(trs).pixel(160, 90));
            qInfo("stinger %-16s best coverage %5.1f%%  %s", qPrintable(id), best * 100, landed ? "landed on new scene" : "DID NOT LAND");
            if (best < 0.97 || !landed) failures << id;
        }
        QVERIFY2(failures.isEmpty(), qPrintable("stingers failing: " + failures.join(", ")));
        QVERIFY(e.setTransition("fade_transition"));
        obs_transition_set(e.transition(), e.sceneSource(e.sceneOrder().first()));
        e.removeScene(a); e.removeScene(b);
    }

    // Every catalog widget must draw something in the gallery preview, and every one must
    // run live inside libobs (frames pushed, chat/events/commands handled) without crashing.
    void everyCatalogWidgetRenders() {
        const auto &cat = widgetCatalog();
        QVERIFY2(cat.size() >= 50, qPrintable(QString::number(cat.size())));
        QStringList empty;
        for (const auto &e : cat) {
            const WidgetConfig cfg = widgetConfigFromJson(e.defaults);
            QCOMPARE(cfg.kind, e.kind);
            const QImage img = WidgetManager::instance().renderPreview(cfg, QSize(480, 270));
            int lit = 0;
            for (int y = 0; y < img.height(); y += 2)
                for (int x = 0; x < img.width(); x += 2) lit += qAlpha(img.pixel(x, y)) > 40;
            if (lit < 20) empty << e.id;
        }
        QVERIFY2(empty.isEmpty(), qPrintable("blank previews: " + empty.join(", ")));

        auto &eng = Engine::instance();
        const QString scene = eng.createScene("Widget Scene");
        QVERIFY(!scene.isEmpty());
        const QString before = eng.programScene();
        eng.setPreviewScene(scene);   // on screen, so animated widgets repaint
        QList<OBSWeakSource> made;
        for (const auto &e : cat) {
            QJsonObject d = e.defaults;
            d["width"] = qMin(e.size.width(), 960); d["height"] = qMin(e.size.height(), 540);
            OBSDataAutoRelease st = obs_data_create_from_json(QJsonDocument(d).toJson().constData());
            obs_sceneitem_t *item = eng.addSource(scene, "bc_widget", "W " + e.id, st);
            QVERIFY2(item, qPrintable(e.id));
            made << OBSGetWeakRef(obs_sceneitem_get_source(item));
        }
        auto &hub = PlatformHub::instance();
        auto chat = [&hub](const QString &user, const QString &text, bool mod = false) {
            ChatMessage m; m.platform = "twitch"; m.user = user; m.id = QString::number(QRandomGenerator::global()->generate()); m.text = text; m.fromMod = mod;
            Q_EMIT hub.chatMessage(m);
        };
        chat("Nightshade", "!vote 2"); chat("Ravenmoor", "!vote 1"); chat("VelvetCrypt", "!join");
        chat("Hollowmoon", "!q what's the next game?"); chat("Ashgrave", "gg gg"); chat("Nightshade", "!win", true);
        for (const QString &t : {"follow", "subscribe", "gift", "cheer", "donation", "raid"}) {
            StreamEvent ev; ev.platform = "twitch"; ev.type = t; ev.user = "Ravenmoor"; ev.amount = t == "cheer" ? 500 : 5;
            Q_EMIT hub.streamEvent(ev);
        }
        auto &wm = WidgetManager::instance();
        wm.forEachOfKind("giveaway", [&wm](obs_source_t *s) { wm.drawGiveawayWinner(s); });
        wm.forEachOfKind("qna", [&wm](obs_source_t *s) { wm.nextQuestion(s); });
        wm.forEachOfKind("counter", [&wm](obs_source_t *s) { wm.adjustCounter(s, 3, 0); });
        wm.forEachOfKind("subathon", [&wm](obs_source_t *s) { wm.addSubathonSeconds(s, 300); });
        wm.refreshAll();
        QElapsedTimer t; t.start();
        while (t.elapsed() < 2500) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(20); }
        QStringList noFrame;
        for (const auto &w : made) {
            OBSSourceAutoRelease s = obs_weak_source_get_source(w);
            QVERIFY(s);
            if (obs_source_get_width(s) == 0) noFrame << QString::fromUtf8(obs_source_get_name(s));
        }
        QVERIFY2(noFrame.isEmpty(), qPrintable("no frame pushed: " + noFrame.join(", ")));
        const ChatWidgetState poll = [&] { ChatWidgetState r; wm.forEachOfKind("poll", [&](obs_source_t *s) { r = wm.chatState(s); }); return r; }();
        QCOMPARE(poll.votes.size(), 2);
        eng.setPreviewScene(before);
        QVERIFY(eng.removeScene(scene));
    }

    // On Windows the shipped build must load both VST hosts: obs-vst (VST 2.x, built by OBS
    // against the same Qt we ship) and obs-vst3 (vendored, built with the app).
    void vstHostsLoaded() {
#ifdef _WIN32
        QVERIFY2(obs_source_get_display_name("vst_filter"), "obs-vst (VST 2.x) did not load — Qt version mismatch?");
        QVERIFY2(obs_source_get_display_name("vst3_filter"), "obs-vst3 did not load");
        qInfo("VST 2.x filter: %s · VST3 filter: %s", obs_source_get_display_name("vst_filter"), obs_source_get_display_name("vst3_filter"));
#else
        QSKIP("VST hosts are a Windows deliverable; checked by the Windows CI engine test");
#endif
    }

    void transitionPresetPersists() {
        auto &e = Engine::instance();
        QVERIFY(e.setTransition("kova_shape:bloodfall"));
        const QString path = e.currentCollectionPath();
        QVERIFY(e.saveCollection(path));
        QVERIFY(e.setTransition("fade_transition"));
        QVERIFY(e.loadCollection(path));
        QCOMPARE(e.transitionId(), QStringLiteral("kova_shape:bloodfall"));
        // HeartCast-era ids map onto the gothic set
        QVERIFY(e.setTransition("bc_luma_heart"));
        QCOMPARE(e.transitionId(), QStringLiteral("kova_shape:cathedral"));
        QVERIFY(e.setTransition("fade_transition"));
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
