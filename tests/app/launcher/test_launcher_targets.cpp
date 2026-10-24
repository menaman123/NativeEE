// tests/app/launcher/test_launcher_targets.cpp -- the Targets page driven: in a HOME where the first run is complete,
// the selector shows exactly the manifest's targets with this machine's preselected; the target named by
// PS2R_TARGETS_TARGET is selected and its Build pressed (an extra install the target needs, VitaSDK, runs first);
// the artefact's path and size are reported. A target this host cannot build must show its reason with Build
// disabled -- reported, not pressed. Report: PS2R_TARGETS_REPORT; screenshots: PS2R_TARGETS_SCREENSHOTS.
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "build_runner.h"
#include "flat_button.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "main_window.h"
#include "segmented_control.h"
#include "target_card.h"
#include "targets_page.h"
#include "title_manifest.h"
using namespace ps2r::launcher;

class TestLauncherTargets : public QObject {
    Q_OBJECT
    LauncherApp* app_ = nullptr;
    MainWindow* window_ = nullptr;
    QJsonObject report_;

    void shot(const QString& name) {
        const QString dir = qEnvironmentVariable("PS2R_TARGETS_SCREENSHOTS");
        if (dir.isEmpty()) return;
        QDir().mkpath(dir);
        window_->screenshot(dir + "/" + name + ".png");
    }
    void writeReport() {
        const QString path = qEnvironmentVariable("PS2R_TARGETS_REPORT");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report_).toJson(QJsonDocument::Indented));
    }

private slots:
    void initTestCase() {
        LauncherStyle::apply();
        qputenv("PS2R_LAUNCHER_BUILD_FROM_SOURCE", "1");   // this driver builds the game: the source-build mode
        app_ = new LauncherApp(this);
        QString err;
        QVERIFY2(app_->loadManifest(qEnvironmentVariable("PS2R_TARGETS_MANIFEST"), &err), qPrintable(err));
        window_ = new MainWindow(app_);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        window_->showPage(2);
        QVERIFY2(app_->installer()->complete(), "the first run is not complete in this HOME (run the cold judge first)");
    }
    void cleanupTestCase() { writeReport(); delete window_; }

    void selectorIsExactlyTheManifest() {
        TargetsPage* page = window_->targetsPage();
        QStringList ids;
        for (const TargetSpec& t : app_->manifest()->targets()) ids << t.id;
        QCOMPARE(page->selector()->values(), ids);
        QCOMPARE(page->selector()->currentValue(), app_->hostTarget());
        QJsonArray cards;
        for (const TargetSpec& t : app_->manifest()->targets()) {
            page->select(t.id);
            TargetCard* c = page->card(t.id);
            QVERIFY(c && c->isVisible());
            QString reason;
            const bool can = BuildRunner::hostCanBuild(t, &reason);
            QJsonObject o;
            o["id"] = t.id;
            o["label"] = t.label;
            o["host_can_build"] = can;
            o["build_enabled"] = c->buildButton()->isEnabled();
            o["status"] = c->statusText();
            if (!can) {
                QVERIFY2(!c->buildButton()->isEnabled(), "a target this host cannot build must be greyed");
                QVERIFY2(c->statusText().contains(reason.left(30)), "the greyed target must say why");
                o["reason"] = reason;
            }
            cards << o;
            shot("targets-" + t.id);
        }
        report_["cards"] = cards;
        writeReport();
    }
    void buildTheRequestedTarget() {
        const QString id = qEnvironmentVariable("PS2R_TARGETS_TARGET");
        if (id.isEmpty()) QSKIP("no PS2R_TARGETS_TARGET: the cards were checked, nothing is built");
        TargetsPage* page = window_->targetsPage();
        page->select(id);
        TargetCard* c = page->card(id);
        QVERIFY(c);
        const TargetSpec* spec = app_->manifest()->target(id);
        QVERIFY(spec);
        QString reason;
        if (!BuildRunner::hostCanBuild(*spec, &reason)) {
            report_["built"] = false;
            report_["cross_reason"] = reason;
            writeReport();
            QSKIP(qPrintable("this host cannot build " + id + ": " + reason));
        }
        QVERIFY(c->buildButton()->isEnabled());
        QElapsedTimer t;
        t.start();
        QTest::mouseClick(c->buildButton(), Qt::LeftButton);
        const int timeoutMs = qEnvironmentVariable("PS2R_TARGETS_TIMEOUT_MIN", "180").toInt() * 60 * 1000;
        bool shotMid = false;
        QVERIFY2(QTest::qWaitFor([&] {
            if (!shotMid && t.elapsed() > 30000) { shot("targets-" + id + "-progress"); shotMid = true; }
            return !app_->installer()->running() && !app_->builder()->running() && t.elapsed() > 2000;
        }, timeoutMs), "the target's install + build did not finish in time");
        const BuildRecord r = app_->builder()->record(id);
        report_["built"] = r.ok;
        report_["artifact"] = r.artifactPath;
        report_["artifact_bytes"] = double(r.artifactBytes);
        report_["seconds"] = double(t.elapsed() / 1000);
        report_["failure"] = r.failure;
        report_["log"] = app_->builder()->currentLogPath();
        for (const InstallStep& s : app_->installer()->steps()) if (s.id == "vitasdk") { report_["vitasdk_state"] = int(s.state); report_["vitasdk_detail"] = s.detail; report_["vitasdk_reason"] = s.reason; report_["vitasdk_bytes"] = double(s.bytes); }
        writeReport();
        shot("targets-" + id + "-done");
        QVERIFY2(r.ok, qPrintable(r.failure));
        QVERIFY(QFileInfo::exists(r.artifactPath));
        QVERIFY(c->showButton()->isVisible());
    }
};

QTEST_MAIN(TestLauncherTargets)
#include "test_launcher_targets.moc"
