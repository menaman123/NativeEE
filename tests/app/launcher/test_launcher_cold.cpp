// tests/app/launcher/test_launcher_cold.cpp -- the cold-run driver a judge points at a manifest: with HOME at an
// empty folder it opens the real window, lets the first run install everything, points the Games folder at the given
// disc image folder, presses the card's Build, then its Play, and waits for the game's title state in the launcher's
// own play log. Zero shell commands by a person: every step is a button of the window. Writes a JSON report.
//
// Environment: PS2R_COLD_MANIFEST (title.json), PS2R_COLD_ISO_DIR (a folder holding the disc image),
// PS2R_COLD_REPORT (the JSON written), PS2R_COLD_SCREENSHOTS (a folder for one PNG per screen),
// PS2R_COLD_INSTALL_TIMEOUT_MIN (default 90), PS2R_COLD_BUILD_TIMEOUT_MIN (default 120), PS2R_COLD_TITLE_TIMEOUT_S (default 300).
#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include "app_data.h"
#include "build_runner.h"
#include "first_run_sheet.h"
#include "flat_button.h"
#include "game_card.h"
#include "game_library.h"
#include "game_process.h"
#include "games_page.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "main_window.h"
#include "settings_page.h"
#include "targets_page.h"
using namespace ps2r::launcher;

class TestLauncherCold : public QObject {
    Q_OBJECT
    LauncherApp* app_ = nullptr;
    MainWindow* window_ = nullptr;
    QJsonObject report_;
    QElapsedTimer wall_;
    QString shots_;
    bool download_ = false;   // PS2R_COLD_DOWNLOAD=1: the launcher's default mode

    int envInt(const char* name, int fallback) {
        const QByteArray v = qgetenv(name);
        return v.isEmpty() ? fallback : v.toInt();
    }
    void shot(const QString& name) {
        if (shots_.isEmpty()) return;
        QDir().mkpath(shots_);
        window_->screenshot(shots_ + "/" + name + ".png");
    }
    void writeReport() {
        const QString path = qEnvironmentVariable("PS2R_COLD_REPORT");
        if (path.isEmpty()) return;
        report_["wall_seconds"] = double(wall_.elapsed() / 1000);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report_).toJson(QJsonDocument::Indented));
    }

private slots:
    void initTestCase() {
        wall_.start();
        shots_ = qEnvironmentVariable("PS2R_COLD_SCREENSHOTS");
        LauncherStyle::apply();
        // PS2R_COLD_DOWNLOAD=1: the default mode (the release's game downloaded); else the source build
        download_ = qEnvironmentVariable("PS2R_COLD_DOWNLOAD") == "1";
        if (!download_) qputenv("PS2R_LAUNCHER_BUILD_FROM_SOURCE", "1");
        app_ = new LauncherApp(this);
        QString err;
        QVERIFY2(app_->loadManifest(qEnvironmentVariable("PS2R_COLD_MANIFEST"), &err), qPrintable(err));
        report_["home"] = qEnvironmentVariable("HOME");
        report_["app_data"] = app_->appData()->root();
        window_ = new MainWindow(app_);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
    }
    void cleanupTestCase() {
        writeReport();
        if (app_->game()->running()) app_->game()->stop();
        delete window_;
    }

    void firstRunInstalls() {
        QElapsedTimer t;
        t.start();
        QVERIFY2(window_->startFirstRunIfNeeded(), "the first run was not needed: HOME is not empty");
        QVERIFY(window_->firstRunSheet()->isOpen());
        shot("01-first-run");
        const int timeoutMs = envInt("PS2R_COLD_INSTALL_TIMEOUT_MIN", 90) * 60 * 1000;
        bool shotMid = false;
        QVERIFY2(QTest::qWaitFor([&] {
            if (!shotMid && t.elapsed() > 20000) { shot("02-first-run-progress"); shotMid = true; }
            return !app_->installer()->running();
        }, timeoutMs), "the installer did not finish in time");
        QJsonObject steps;
        for (const InstallStep& s : app_->installer()->steps()) {
            QJsonObject o;
            o["state"] = int(s.state);
            o["detail"] = s.detail;
            o["reason"] = s.reason;
            o["bytes"] = double(s.bytes);
            steps[s.id] = o;
        }
        report_["install_steps"] = steps;
        report_["install_seconds"] = double(t.elapsed() / 1000);
        report_["installed_release"] = app_->installer()->installedRelease();
        report_["installed_game"] = app_->installer()->installedGameExe();
        report_["download_bytes"] = double(app_->installer()->totalDownloadedBytes());
        report_["on_disk_bytes"] = double(app_->installer()->totalOnDiskBytes());
        writeReport();
        for (const InstallStep& s : app_->installer()->steps()) {
            if (!s.optional) QVERIFY2(s.state == InstallStep::Done, qPrintable(s.name + ": " + s.reason));
        }
        QVERIFY(app_->installer()->complete());
        QVERIFY(QTest::qWaitFor([&] { return !window_->firstRunSheet()->isOpen(); }, 5000));
        // a second start is a no-op: the pins match
        QVERIFY(app_->installer()->upToDate());
    }
    void discIsFound() {
        const QString dir = qEnvironmentVariable("PS2R_COLD_ISO_DIR");
        QVERIFY(!dir.isEmpty());
        // the user points the Games folder at the folder holding the disc (the Change… button's result)
        app_->setFolder("games", dir);
        app_->scan();
        QVERIFY2(QTest::qWaitFor([&] { return !app_->library()->games().isEmpty(); }, 120000), "no disc image was identified in the folder");
        QVERIFY(QTest::qWaitFor([&] { return !window_->gamesPage()->cards().isEmpty(); }, 5000));
        const GameEntry& g = app_->library()->games().first();
        report_["disc"] = g.path;
        report_["disc_fingerprint"] = g.fingerprintFile;
        QCOMPARE(app_->nextAction(g.path), download_ ? LauncherApp::Play : LauncherApp::Build);
        shot("03-games-found");
    }
    void buildFromTheCard() {
        if (download_) QSKIP("the downloaded game needs no build");
        const QString iso = app_->library()->games().first().path;
        GameCard* card = window_->gamesPage()->card(iso);
        QVERIFY(card);
        QCOMPARE(card->actionButton()->text(), QStringLiteral("Build"));
        QElapsedTimer t;
        t.start();
        QTest::mouseClick(card->actionButton(), Qt::LeftButton);
        QVERIFY(QTest::qWaitFor([&] { return app_->builder()->running(); }, 10000));
        const int timeoutMs = envInt("PS2R_COLD_BUILD_TIMEOUT_MIN", 120) * 60 * 1000;
        bool shotMid = false;
        QVERIFY2(QTest::qWaitFor([&] {
            if (!shotMid && t.elapsed() > 60000) { card = window_->gamesPage()->card(iso); if (card) shot("04-build-progress"); shotMid = true; }
            return !app_->builder()->running();
        }, timeoutMs), "the build did not finish in time");
        const BuildRecord r = app_->builder()->record(app_->hostTarget());
        report_["build_ok"] = r.ok;
        report_["build_seconds"] = double(r.seconds);
        report_["build_commit"] = r.sourceCommit;
        report_["build_binary"] = r.binaryPath;
        report_["build_failure"] = r.failure;
        report_["build_log"] = app_->builder()->currentLogPath();
        writeReport();
        QVERIFY2(r.ok, qPrintable(r.failure));
        QVERIFY(app_->builder()->upToDate(app_->hostTarget()));
        QCOMPARE(app_->nextAction(iso), LauncherApp::Play);
        QVERIFY(QTest::qWaitFor([&] { card = window_->gamesPage()->card(iso); return card && card->actionButton()->text() == QStringLiteral("Play"); }, 5000));
        shot("05-games-ready");
    }
    void playReachesTitle() {
        const QString iso = app_->library()->games().first().path;
        GameCard* card = window_->gamesPage()->card(iso);
        QVERIFY(card);
        QCOMPARE(card->actionButton()->text(), QStringLiteral("Play"));
        QElapsedTimer t;
        t.start();
        QTest::mouseClick(card->actionButton(), Qt::LeftButton);
        QVERIFY(QTest::qWaitFor([&] { return app_->game()->running(); }, 10000));
        report_["play_log"] = app_->game()->logPath();
        const int timeoutMs = envInt("PS2R_COLD_TITLE_TIMEOUT_S", 300) * 1000;
        const bool reached = QTest::qWaitFor([&] { return app_->game()->reachedTitle() || !app_->game()->running(); }, timeoutMs);
        report_["title_reached"] = app_->game()->reachedTitle();
        report_["title_seconds"] = double(t.elapsed() / 1000);
        shot("06-playing");
        writeReport();
        QVERIFY2(reached && app_->game()->reachedTitle(), "the game did not reach its title state");
        app_->game()->stop();
        QVERIFY(QTest::qWaitFor([&] { return !app_->game()->running(); }, 15000));
        QVERIFY(app_->library()->games().first().lastPlayed.isValid());
    }
    void otherScreens() {
        window_->showPage(1);
        QTest::qWait(200);
        shot("07-settings");
        window_->showPage(2);
        QTest::qWait(200);
        shot("08-targets");
        window_->showPage(3);
        QTest::qWait(200);
        shot("09-about");
        window_->showPage(0);
        writeReport();
    }
};

QTEST_MAIN(TestLauncherCold)
#include "test_launcher_cold.moc"
