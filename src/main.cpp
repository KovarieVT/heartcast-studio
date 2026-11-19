// HeartCast Studio — entry point
#include "BuildInfo.h"
#include "app/Theme.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "overlays/WidgetManager.h"
#include "platforms/PlatformHub.h"
#include "plugins/PluginManager.h"
#include "ui/MainWindow.h"
#include "ui/Pages.h"
#include "ui/SettingsPage.h"
#include "ui/SplashScreen.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace bc;

// Hard guarantee that a hung startup never leaves the splash on screen forever:
// a watchdog thread notices if no loading progress happens for 60 s.
static std::atomic<qint64> g_lastProgress{0};
static std::atomic<bool> g_startupDone{false};

static qint64 nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static void startWatchdog() {
    g_lastProgress = nowMs();
    std::thread([] {
        while (!g_startupDone) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!g_startupDone && nowMs() - g_lastProgress > 60000) {
                qCritical("Startup watchdog: no progress for 60 s, giving up");
#ifdef _WIN32
                const std::wstring msg = QStringLiteral("HeartCast Studio is stuck while starting (usually a graphics driver or a broken plugin).\n\n"
                                                        "Logs: %1\n\nThe app will now close. Try updating your GPU driver or removing recently added plugins.")
                                         .arg(QDir::toNativeSeparators(Paths::logs())).toStdWString();
                MessageBoxW(nullptr, msg.c_str(), L"HeartCast Studio", MB_ICONWARNING | MB_OK | MB_TOPMOST);
#endif
                std::_Exit(3);
            }
        }
    }).detach();
}

int main(int argc, char **argv) {
    Q_INIT_RESOURCE(resources);   // resources live in the static core library
    // Settings must be read before QApplication for UI scaling.
    Settings::instance().load(Paths::configFile());
    const double scale = Settings::instance().d("appearance.uiScale");
    if (scale > 0.5 && std::abs(scale - 1.0) > 0.01) qputenv("QT_SCALE_FACTOR", QByteArray::number(scale));
#ifdef __linux__
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");   // libobs needs X11/EGL on Linux dev builds
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(BC_APP_EXE);
    QApplication::setApplicationDisplayName(BC_APP_NAME);
    QApplication::setOrganizationName(BC_APP_ORG);
    QApplication::setApplicationVersion(BC_APP_VERSION);
    QApplication::setWindowIcon(QIcon(":/icons/logo_256.png"));
    QApplication::setQuitOnLastWindowClosed(false);

    // ---- single instance: forward to the running copy instead of starting twice
    const QString serverName = QStringLiteral("HeartCastStudio-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) { probe.write("raise"); probe.waitForBytesWritten(300); return 0; }
    }
    QLocalServer instanceServer;
    QLocalServer::removeServer(serverName);
    instanceServer.listen(serverName);

    Logger::init();
    theme::apply(app, Settings::instance().s("appearance.accent"), Settings::instance().i("appearance.fontPt"));
    applyProcessPriority();

    // ---- crash recovery marker
    const QString lockPath = Paths::dataRoot() + "/running.lock";
    const bool uncleanExit = QFile::exists(lockPath);
    { QFile lock(lockPath); lock.open(QIODevice::WriteOnly); lock.write(QByteArray::number(QCoreApplication::applicationPid())); }

    auto *splash = new SplashScreen(Settings::instance().b("general.startupAnimations"));
    splash->show();
    app.processEvents();
    startWatchdog();
    QObject::connect(splash, &SplashScreen::stalled, [] { qWarning("Splash: loading slower than expected"); });

    Logger::installObsLogHandler();
    const bool ok = Engine::instance().startup([splash](int pct, const QString &msg) {
        g_lastProgress = nowMs();
        splash->setProgress(pct, msg);
    });
    QString engineErr;
    QObject::connect(&Engine::instance(), &Engine::engineError, [&engineErr](const QString &m) { engineErr = m; });
    if (!ok) {
        g_startupDone = true;
        splash->close();
        QMessageBox box(QMessageBox::Critical, BC_APP_NAME,
                        QObject::tr("The streaming engine couldn't start.\n\n%1\n\nUpdating your graphics driver usually fixes this.")
                            .arg(engineErr.isEmpty() ? QObject::tr("See the log for details.") : engineErr));
        auto *logs = box.addButton(QObject::tr("Open log"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == static_cast<QAbstractButton *>(logs)) QDesktopServices::openUrl(QUrl::fromLocalFile(Logger::currentLogFile()));
        QFile::remove(lockPath);
        return 1;
    }
    g_startupDone = true;

    PlatformHub::instance().init();
    WidgetManager::instance().init();
    MediaLibraryPage::ensureStarterAssets();
    app.installNativeEventFilter(&HotkeyManager::instance());

    auto *win = new MainWindow;
    QObject::connect(&instanceServer, &QLocalServer::newConnection, win, [win, &instanceServer] {
        if (QLocalSocket *s = instanceServer.nextPendingConnection()) { s->deleteLater(); win->showNormal(); win->raise(); win->activateWindow(); }
    });
    splash->finish(win);

    QTimer::singleShot(1200, win, [win, uncleanExit] {
        if (uncleanExit) {
            QStringList dumps;
            for (const auto &fi : QDir(Paths::crashes()).entryInfoList({"*.dmp"}, QDir::Files, QDir::Time)) dumps << fi.fileName();
            win->showCrashRecoveryNotice(dumps.isEmpty() ? QObject::tr("No crash dump was written.") : QObject::tr("Latest dump: %1").arg(dumps.first()));
        }
        if (!Settings::instance().b("general.firstRunComplete")) win->runFirstTimeWizard();
    });

    QObject::connect(win, &QObject::destroyed, &app, &QApplication::quit);
    win->setAttribute(Qt::WA_DeleteOnClose);
    const int rc = app.exec();

    // ---- orderly shutdown: outputs → plugins → widgets → engine
    PluginManager::instance().unloadAll();
    PlatformHub::instance().stopChat();
    WidgetManager::instance().shutdown();
    Engine::instance().shutdown();
    Settings::instance().save();
    QFile::remove(lockPath);
    qInfo("Clean exit (%d)", rc);
    Logger::shutdown();
    return rc;
}
