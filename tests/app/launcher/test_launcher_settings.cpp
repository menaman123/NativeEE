// tests/app/launcher/test_launcher_settings.cpp -- the Settings page against the game's options file, row by row: for
// every row of the manifest, every value set through the row's own control lands in the file under the row's key,
// and a value written to the file by someone else (the game's Options menu) is what the row shows after a reload.
// Writes a JSON report of every row and value checked (PS2R_SETTINGS_REPORT) for the title's judge.
//
// Environment: PS2R_SETTINGS_MANIFEST (title.json), HOME (the options file is under it), PS2R_SETTINGS_REPORT.
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "flat_button.h"
#include "ini_settings.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "main_window.h"
#include "segmented_control.h"
#include "settings_page.h"
#include "settings_row.h"
#include "switch_control.h"
#include "title_manifest.h"
using namespace ps2r::launcher;

class TestLauncherSettings : public QObject {
    Q_OBJECT
    LauncherApp* app_ = nullptr;
    MainWindow* window_ = nullptr;
    QJsonArray rows_;

    void setThroughControl(SettingsRow* row, const QString& value) {
        if (row->segmented()) {
            const int i = row->segmented()->values().indexOf(value);
            QVERIFY(i >= 0);
            // a click on the segment already selected changes nothing (as for a user): go through a neighbour first
            if (row->segmented()->currentIndex() == i) row->segmented()->setCurrentIndex(i == 0 ? 1 : i - 1, true);
            row->segmented()->setCurrentIndex(i, true);   // the click on that segment
        } else if (row->toggle()) {
            const bool on = value == "on";
            if (row->toggle()->isChecked() == on) row->toggle()->click();   // already there: go through the other state first
            row->toggle()->click();
        } else {
            // the popup: the menu's action for that value, taken directly (QMenu::exec blocks on a real menu)
            row->setValue(value);
            emit row->valueChanged(row->spec().key, value);
        }
    }

private slots:
    void initTestCase() {
        LauncherStyle::apply();
        app_ = new LauncherApp(this);
        QString err;
        QVERIFY2(app_->loadManifest(qEnvironmentVariable("PS2R_SETTINGS_MANIFEST"), &err), qPrintable(err));
        window_ = new MainWindow(app_);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        window_->showPage(1);
    }
    void cleanupTestCase() {
        const QString path = qEnvironmentVariable("PS2R_SETTINGS_REPORT");
        if (!path.isEmpty()) {
            QJsonObject report;
            report["settings_file"] = app_->settingsFilePath();
            report["rows"] = rows_;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        }
        delete window_;
    }
    void everyRowRoundTrips() {
        SettingsPage* page = window_->settingsPage();
        IniSettings ini(app_->settingsFilePath());
        const QVector<SettingsRowSpec> specs = app_->manifest()->allSettingsRows();
        QVERIFY(!specs.isEmpty());
        QCOMPARE(page->rows().size(), specs.size());
        for (const SettingsRowSpec& spec : specs) {
            SettingsRow* row = page->row(spec.key);
            QVERIFY2(row, qPrintable(spec.key));
            QJsonObject r;
            r["key"] = spec.key;
            r["label"] = spec.label;
            QJsonArray values;
            // the default shows before the file has the key
            QCOMPARE(row->value(), spec.defaultValue);
            for (const SettingsChoice& c : spec.choices) {
                // launcher -> file
                setThroughControl(row, c.value);
                QCOMPARE(row->value(), c.value);
                QVERIFY2(ini.get(spec.key) == c.value, qPrintable(spec.key + " expected " + c.value + " in the file, got " + ini.get(spec.key)));
                values << c.value;
            }
            // file -> launcher: the game's menu wrote the first value; the page reads it back
            const QString back = spec.choices.first().value;
            QVERIFY(ini.set(spec.key, back));
            page->reload();
            QCOMPARE(row->value(), back);
            r["values"] = values;
            r["round_trip"] = true;
            rows_ << r;
        }
        // the frame-rate rule when the manifest has both rows
        SettingsRow* rate = page->row("video.frame_rate");
        SettingsRow* vsync = page->row("video.vsync");
        if (rate && vsync) {
            setThroughControl(rate, "uncapped");
            QVERIFY(!vsync->toggle()->isEnabled());
            setThroughControl(rate, "120");
            QVERIFY(vsync->toggle()->isEnabled());
            QCOMPARE(ini.get("video.frame_rate"), QStringLiteral("120"));
        }
    }
};

QTEST_MAIN(TestLauncherSettings)
#include "test_launcher_settings.moc"
