#include <QtTest>
#include <QTemporaryDir>

#include "auth/OAuth.h"
#include "core/HotkeyManager.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"
#include "platforms/Twitch.h"
#include "updater/Updater.h"

using namespace bc;

class UnitTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        Q_INIT_RESOURCE(resources);
        QVERIFY(m_tmp.isValid());
        Settings::instance().load(m_tmp.filePath("config.json"));
    }

    void settingsDefaultsAndDottedKeys() {
        auto &s = Settings::instance();
        QCOMPARE(s.i("video.baseWidth"), 1920);
        s.set("output.bitrate", 7500);
        QCOMPARE(s.i("output.bitrate"), 7500);
        s.set("brand.new.nested", QString("yes"));
        QCOMPARE(s.s("brand.new.nested"), QString("yes"));
        QVERIFY(s.save());
        Settings::instance().load(m_tmp.filePath("config.json"));
        QCOMPARE(s.i("output.bitrate"), 7500);           // persisted
        QCOMPARE(s.i("video.fpsNum"), 60);               // defaults merged back in
    }

    void settingsCorruptFileFallsBack() {
        QFile f(m_tmp.filePath("bad.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not json");
        f.close();
        QVERIFY(Settings::instance().load(m_tmp.filePath("bad.json")));
        QCOMPARE(Settings::instance().i("video.baseWidth"), 1920);
        QVERIFY(QFile::exists(m_tmp.filePath("bad.json.corrupt")));
        Settings::instance().load(m_tmp.filePath("config.json"));
    }

    void settingsExportImportRoundTrip() {
        auto &s = Settings::instance();
        s.set("appearance.accent", QString("#77CDB1"));
        QVERIFY(s.exportTo(m_tmp.filePath("export.json")));
        s.set("appearance.accent", QString("#000000"));
        QString err;
        QVERIFY(s.importFrom(m_tmp.filePath("export.json"), &err));
        QCOMPARE(s.s("appearance.accent"), QString("#77CDB1"));
    }

    void pkceChallengeIsS256Base64Url() {
        // Expected value computed independently: base64url(sha256(verifier)) without padding.
        QCOMPARE(LoopbackPkceFlow::challengeFor("dBjftJeZ4CVP-mJ92K9TqyrTz5i7sY9cr5xSLiPfuTkM"),
                 QByteArray("DxoUiEv-2KcpGx1JtQg4-qMpTaJDF7YkbwqDuumdDC0"));
        const QByteArray v = LoopbackPkceFlow::makeVerifier();
        QVERIFY(v.size() >= 43 && v.size() <= 128);   // RFC 7636 length bounds
        QVERIFY(!v.contains('=') && !v.contains('+') && !v.contains('/'));
    }

    void tokenSetExpiry() {
        TokenSet t = TokenSet::fromTokenResponse(QJsonObject{{"access_token", "a"}, {"refresh_token", "r"}, {"expires_in", 60}});
        QVERIFY(t.valid());
        QVERIFY(t.expiringSoon());   // < 5 minutes left
        t = TokenSet::fromTokenResponse(QJsonObject{{"access_token", "a"}, {"expires_in", 7200}, {"scope", QJsonArray{"x", "y"}}});
        QVERIFY(!t.expiringSoon());
        QCOMPARE(t.scope, QString("x y"));
    }

    void twitchIrcParsing() {
        const QString line = "@badge-info=;badges=moderator/1,subscriber/12;color=#FF69B4;display-name=Mochi\\sMochi;"
                             "id=abc-123;user-id=4242 :mochi!mochi@mochi.tmi.twitch.tv PRIVMSG #me :hello there :) ♡";
        const ChatMessage m = TwitchAccount::parseIrcPrivmsg(line);
        QCOMPARE(m.user, QString("Mochi Mochi"));
        QCOMPARE(m.text, QString("hello there :) ♡"));
        QCOMPARE(m.id, QString("abc-123"));
        QCOMPARE(m.userId, QString("4242"));
        QVERIFY(m.badges.contains("moderator"));
        QVERIFY(m.fromMod);
    }

    void chatFilters() {
        auto &hub = PlatformHub::instance();
        hub.setBlockedWords({"badword"});
        ChatMessage a; a.platform = "twitch"; a.userId = "1"; a.user = "u"; a.text = "this has a BadWord in it";
        QVERIFY(hub.filterMessage(a));
        QCOMPARE(a.hiddenReason, QString("blocked word"));
        ChatMessage ok; ok.platform = "twitch"; ok.userId = "2"; ok.text = "hi cutie";
        QVERIFY(!hub.filterMessage(ok));
        ChatMessage rep = ok;
        QVERIFY(hub.filterMessage(rep));             // same text again within 15 s
        int hidden = 0;
        for (int i = 0; i < 8; ++i) { ChatMessage f; f.platform = "youtube"; f.userId = "3"; f.text = QString("msg %1").arg(i); hidden += hub.filterMessage(f); }
        QVERIFY(hidden >= 2);                        // >5 msgs in 5 s = flooding
        ChatMessage mod; mod.platform = "twitch"; mod.userId = "9"; mod.fromMod = true; mod.text = "badword";
        QVERIFY(!hub.filterMessage(mod));            // mods are never filtered
    }

    void destinationJsonRoundTrip() {
        Destination d; d.id = "x1"; d.platform = "kick"; d.name = "Kick"; d.server = "rtmps://example/app";
        d.keyRef = "streamkey.x1"; d.separateEncoder = true; d.bitrate = 3500;
        const Destination r = Destination::fromJson(d.toJson());
        QCOMPARE(r.platform, d.platform); QCOMPARE(r.server, d.server); QCOMPARE(r.bitrate, 3500);
        QVERIFY(r.separateEncoder);
        QVERIFY(!r.toJson().contains("key"));        // the key itself is never in settings JSON
    }

    void platformCatalogIsHonest() {
        QVERIFY(PlatformCatalog::all().size() >= 9);
        for (const QString id : {"kick", "tiktok", "facebook", "instagram"}) {
            const PlatformInfo p = PlatformCatalog::get(id);
            QCOMPARE(p.auth, QString("manual"));     // no fake OAuth for platforms without public APIs
            QVERIFY(!p.has("chat_read"));
        }
        QVERIFY(PlatformCatalog::get("twitch").has("chat_read"));
    }

    void hotkeyVirtualKeys() {
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_A), 0x41u);
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_F9), 0x78u);
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_5), 0x35u);
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_unknown), 0u);
    }

    void hotkeyConflictDetection() {
        auto &hk = HotkeyManager::instance();
        hk.registerAction("t.one", "One", "Test", [] {});
        hk.registerAction("t.two", "Two", "Test", [] {});
        Settings::instance().set("hotkeys.t.one", QString("Ctrl+Shift+F9"));
        QVERIFY(hk.conflictFor("t.two", QKeySequence("Ctrl+Shift+F9")).contains("One"));
        QVERIFY(hk.conflictFor("t.two", QKeySequence("Ctrl+Shift+F10")).isEmpty());
    }

    void updaterVersionCompare() {
        QVERIFY(Updater::isNewer("0.2.0", "0.1.9"));
        QVERIFY(Updater::isNewer("1.0.0", "0.10.0"));
        QVERIFY(!Updater::isNewer("0.1.0", "0.1.0"));
        QVERIFY(!Updater::isNewer("0.0.9", "0.1.0"));
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_MAIN(UnitTests)
#include "test_unit.moc"
