// tests/app/launcher/test_launcher_screens.cpp -- the window offscreen with a synthetic manifest and an empty HOME:
// every page exists and shows what the design says, no placeholder text or emoji anywhere, the Settings rows round-trip
// to the options file, the frame-rate rule locks vertical sync, a screenshot is written, the sheets open and close.
#include <QtTest/QtTest>
#include <QLabel>
#include <QTemporaryDir>
#include "about_page.h"
#include "checklist_row.h"
#include "folder_row.h"
#include "details_sheet.h"
#include "empty_state.h"
#include "first_run_sheet.h"
#include "flat_button.h"
#include "games_page.h"
#include "ini_settings.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "main_window.h"
#include "segmented_control.h"
#include "settings_page.h"
#include "settings_row.h"
#include "sidebar.h"
#include "switch_control.h"
#include "target_card.h"
#include "targets_page.h"
using namespace ps2r::launcher;

namespace {
const char* kManifest = R"JSON({
  "id": "SLES-52568", "name": "Crash Twinsanity", "product": "TwinsanityEvolved", "app_data_folder": "TwinsanityEvolved",
  "description": "A test description.",
  "disc": { "fingerprint": { "file": "SLES_525.68", "region": "PAL", "id": "SLES-52568" } },
  "source": { "url": "/tmp/nowhere" },
  "targets": { "macos": { "label": "macOS", "host": "macos", "configure": ["-G", "Ninja"], "binary": "twinsanityevolved" },
               "windows": { "label": "Windows", "host": "windows", "artifact": "zip", "cross_reason": "The Windows build needs clang-cl, which exists only on Windows." },
               "vita": { "label": "PS Vita", "host": "any", "artifact": "vpk", "preset": "vita-release", "needs": ["vitasdk"], "install_text": "Install the .vpk with VitaShell." } },
  "run": { "iso_flag": "--iso" },
  "settings": { "file": "~/.te-test/ps2r.ini", "groups": [ { "title": "Picture", "rows": [
    { "key": "video.aspect", "label": "Aspect ratio", "kind": "choice", "default": "auto", "choices": [ {"value": "auto", "label": "Auto"}, {"value": "16:9"} ] },
    { "key": "video.frame_rate", "label": "Frame rate", "kind": "choice", "default": "60", "choices": [ {"value": "30"}, {"value": "60"}, {"value": "120"}, {"value": "144"}, {"value": "240"}, {"value": "uncapped", "label": "Uncapped"} ] },
    { "key": "video.vsync", "label": "Vertical sync", "kind": "switch", "default": "off" },
    { "key": "game.language", "label": "Language", "kind": "choice", "default": "english", "choices": [ {"value": "english"}, {"value": "french"}, {"value": "german"}, {"value": "spanish"}, {"value": "italian"}, {"value": "portuguese"}, {"value": "dutch"} ] } ] } ] },
  "folders": { "games": { "label": "Games", "default": "${app_data}/Games" }, "mods": { "label": "Mods", "default": "${app_data}/Mods" }, "memory_cards": { "label": "Memory cards", "default": "~/.te-test/memcards" } },
  "scan": { "folders": ["~/Downloads"], "removable": false }
})JSON";

bool hasEmoji(const QString& s) {
    for (const QChar& c : s) {
        if (c.isHighSurrogate()) return true;   // every emoji is outside the BMP
        if (c.unicode() >= 0x2600 && c.unicode() <= 0x27BF) return true;   // the misc symbols / dingbats block
    }
    return false;
}
}  // namespace

class TestLauncherScreens : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    LauncherApp* app_ = nullptr;
    MainWindow* window_ = nullptr;

private slots:
    void initTestCase() {
        qputenv("HOME", home_.path().toUtf8());
        qputenv("USERPROFILE", home_.path().toUtf8());    // AppData::homePath() on Windows
        qputenv("LOCALAPPDATA", home_.path().toUtf8());   // the app-data root on Windows
        LauncherStyle::apply();
        QFile f(home_.path() + "/title.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kManifest);
        f.close();
        app_ = new LauncherApp(this);
        QString err;
        QVERIFY2(app_->loadManifest(home_.path() + "/title.json", &err), qPrintable(err));
        window_ = new MainWindow(app_);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
    }
    void cleanupTestCase() { delete window_; }

    void pagesAndSidebar() {
        QCOMPARE(window_->minimumSize(), QSize(1024, 640));
        QCOMPARE(window_->sidebar()->pageName(0), QStringLiteral("Games"));
        QCOMPARE(window_->sidebar()->pageName(3), QStringLiteral("About"));
        for (int i = 0; i < 4; ++i) { window_->showPage(i); QCOMPARE(window_->currentPage(), i); QCOMPARE(window_->sidebar()->current(), i); }
        window_->showPage(0);
    }
    void noPlaceholderTextOrEmoji() {
        for (int i = 0; i < 4; ++i) {
            window_->showPage(i);
            for (QLabel* l : window_->findChildren<QLabel*>()) {
                const QString t = l->text();
                QVERIFY2(!t.contains("TODO") && !t.contains("Lorem") && !t.contains("placeholder", Qt::CaseInsensitive) && !t.contains("TextLabel"), qPrintable(t));
                QVERIFY2(!hasEmoji(t), qPrintable(t));
            }
            for (FlatButton* b : window_->findChildren<FlatButton*>()) {
                QVERIFY2(!b->text().isEmpty() || !b->isVisible(), "a visible button without text");
                QVERIFY2(!hasEmoji(b->text()), qPrintable(b->text()));
            }
        }
        window_->showPage(0);
    }
    void gamesEmptyState() {
        window_->showPage(0);
        GamesPage* g = window_->gamesPage();
        QVERIFY(g->emptyState()->isVisible());
        QVERIFY(g->cards().isEmpty());
        QVERIFY(g->folderRow("games"));
        QVERIFY(g->folderRow("mods"));
        QVERIFY(g->folderRow("memory_cards"));
        QVERIFY(g->folderRow("games")->path().endsWith("/Games"));
        // a file that is not a disc: the error state in words
        QFile junk(home_.path() + "/junk.iso");
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(40 * 1024 * 1024, 'x'));
        junk.close();
        g->addDiscFromPath(home_.path() + "/junk.iso");
        QVERIFY(g->emptyState()->isVisible());
        QVERIFY(g->cards().isEmpty());
        bool errorShown = false;
        for (QLabel* l : g->emptyState()->findChildren<QLabel*>()) if (l->text().contains("cannot be read as a disc image")) errorShown = true;
        QVERIFY(errorShown);
    }
    void settingsRoundTrip() {
        window_->showPage(1);
        SettingsPage* s = window_->settingsPage();
        QCOMPARE(s->rows().size(), 4);
        SettingsRow* rate = s->row("video.frame_rate");
        QVERIFY(rate && rate->segmented());
        QCOMPARE(rate->segmented()->values(), QStringList({"30", "60", "120", "144", "240", "uncapped"}));
        QCOMPARE(rate->value(), QStringLiteral("60"));   // the manifest's default before any file exists
        rate->segmented()->setCurrentIndex(2, true);     // 120, as a click would
        IniSettings ini(app_->settingsFilePath());
        QCOMPARE(ini.get("video.frame_rate"), QStringLiteral("120"));
        // the file changed behind the launcher's back (the game's Options menu): the page reads it when shown
        QVERIFY(ini.set("video.frame_rate", "240"));
        s->reload();
        QCOMPARE(rate->value(), QStringLiteral("240"));
        SettingsRow* vsync = s->row("video.vsync");
        QVERIFY(vsync && vsync->toggle());
        vsync->toggle()->click();
        QCOMPARE(ini.get("video.vsync"), QStringLiteral("on"));
        QVERIFY(vsync->toggle()->isEnabled());
        rate->segmented()->setCurrentIndex(5, true);
        QCOMPARE(ini.get("video.frame_rate"), QStringLiteral("uncapped"));
        QVERIFY(!vsync->toggle()->isEnabled());   // locked while uncapped, as in the game's menu
        rate->segmented()->setCurrentIndex(1, true);
        QVERIFY(vsync->toggle()->isEnabled());
        SettingsRow* lang = s->row("game.language");
        QVERIFY(lang && lang->popup());   // seven values: a popup, not a segmented bar
        QCOMPARE(lang->value(), QStringLiteral("english"));
        QVERIFY(s->memoryCardsRow());
        QVERIFY(s->memoryCardsRow()->path().endsWith("/.te-test/memcards"));
    }
    void targets() {
        window_->showPage(2);
        TargetsPage* t = window_->targetsPage();
        QCOMPARE(t->selector()->values(), QStringList({"macos", "windows", "vita"}));
        QCOMPARE(t->selector()->currentValue(), app_->hostTarget());
        t->select("windows");
        TargetCard* win = t->card("windows");
        QVERIFY(win->isVisible());
#ifdef Q_OS_MACOS
        QVERIFY(!win->buildButton()->isEnabled());
        QVERIFY(win->statusText().contains("exists only on Windows"));
#endif
        t->select("vita");
        QVERIFY(t->card("vita")->statusText().contains("VitaSDK"));
        QVERIFY(!t->card("windows")->isVisible());
    }
    void aboutAndSheets() {
        window_->showPage(3);
        QVERIFY(window_->aboutPage()->removeButton());
        window_->detailsSheet()->showLog("Details", "line one\nline two");
        QVERIFY(window_->detailsSheet()->isOpen());
        QCOMPARE(window_->detailsSheet()->text(), QStringLiteral("line one\nline two"));
        window_->detailsSheet()->closeSheet();
        QVERIFY(!window_->detailsSheet()->isOpen());
        window_->firstRunSheet()->open();
        QVERIFY(window_->firstRunSheet()->isOpen());
        QVERIFY(window_->firstRunSheet()->row(0));
        QTest::keyClick(window_->firstRunSheet()->row(0)->window(), Qt::Key_Escape);
    }
    void screenshot() {
        window_->showPage(0);
        const QString p = window_->screenshot(home_.path() + "/shot.png");
        QVERIFY(QFileInfo(p).size() > 1000);
        // PS2R_SCREENS_OUT=DIR: one PNG per page and the first-run sheet, for checking the layout by eye (any scale factor)
        const QString out = qEnvironmentVariable("PS2R_SCREENS_OUT");
        if (out.isEmpty()) return;
        QDir().mkpath(out);
        window_->firstRunSheet()->close();
        window_->detailsSheet()->closeSheet();
        const char* names[] = {"games", "settings", "targets", "about"};
        for (int i = 0; i < 4; ++i) {
            window_->showPage(i);
            QTest::qWait(150);
            window_->screenshot(out + "/" + names[i] + ".png");
        }
        window_->showPage(0);
        window_->firstRunSheet()->open();
        QTest::qWait(400);
        window_->screenshot(out + "/first-run.png");
        window_->firstRunSheet()->close();
    }
    void darkFollowsTheme() {
        LauncherTheme::instance().setDark(true);
        QVERIFY(LauncherTheme::instance().dark());
        QCOMPARE(LauncherTheme::instance().color(LauncherTheme::Accent).name(), QStringLiteral("#f0693a"));
        LauncherTheme::instance().setDark(false);
        QCOMPARE(LauncherTheme::instance().color(LauncherTheme::Accent).name(), QStringLiteral("#d9541e"));
    }
};

QTEST_MAIN(TestLauncherScreens)
#include "test_launcher_screens.moc"
