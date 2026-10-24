// tests/app/launcher/test_launcher_screens.cpp -- the window offscreen at the minimum 1024x640 with a neutral manifest,
// an empty HOME and the fake prepare runner: every first-run page (empty, error and card states, the folder, the
// computer, Preparing with progress, Preparing failed with Try again and Details, Ready) and every page after setup
// (Games, Save States empty and with a state, Settings with its lock rule and without a folder, About, the Forget
// sheet, dark), a PNG of each written to PS2R_LAUNCHER_SCREENS_DIR, and no codename / developer vocabulary /
// placeholder / emoji in any visible string (the strings are also dumped to strings.txt for tools/launcher_lint.py).
#include <QtTest/QtTest>
#include <QAbstractButton>
#include <QLabel>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "about_page.h"
#include "details_sheet.h"
#include "disc_list.h"
#include "empty_state.h"
#include "first_run_flow.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_card.h"
#include "game_folder.h"
#include "game_library.h"
#include "games_page.h"
#include "ini_settings.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "main_window.h"
#include "prepare_runner.h"
#include "progress_panel.h"
#include "save_state_card.h"
#include "save_states_page.h"
#include "segmented_control.h"
#include "settings_page.h"
#include "settings_row.h"
#include "sidebar.h"
#include "switch_control.h"
using namespace ps2r::launcher;

namespace {
QByteArray syntheticIso(const QList<QPair<QString, QByteArray>>& files) {
    QByteArray img(2048 * (20 + files.size()), '\0');
    QByteArray pvd(2048, '\0');
    pvd[0] = 1;
    pvd.replace(1, 5, "CD001");
    pvd[6] = 1;
    pvd.replace(40, 32, QByteArray("EXAMPLE                         "));
    QByteArray root(34, '\0');
    root[0] = 34;
    root[2] = 18;
    root[10] = 0; root[11] = 8;
    root[25] = 2;
    root[32] = 1; root[33] = 0;
    pvd.replace(156, 34, root);
    img.replace(16 * 2048, 2048, pvd);
    QByteArray term(2048, '\0');
    term[0] = '\xff';
    term.replace(1, 5, "CD001");
    img.replace(17 * 2048, 2048, term);
    QByteArray dir;
    int sector = 19;
    for (const auto& f : files) {
        const QByteArray id = (f.first + ";1").toLatin1();
        QByteArray rec(33 + id.size() + (id.size() % 2 == 0 ? 1 : 0), '\0');
        rec[0] = char(rec.size());
        rec[2] = char(sector & 0xff); rec[3] = char((sector >> 8) & 0xff);
        const quint32 len = quint32(f.second.size());
        rec[10] = char(len & 0xff); rec[11] = char((len >> 8) & 0xff); rec[12] = char((len >> 16) & 0xff); rec[13] = char((len >> 24) & 0xff);
        rec[32] = char(id.size());
        rec.replace(33, id.size(), id);
        dir += rec;
        img.replace(sector * 2048, f.second.size(), f.second);
        ++sector;
    }
    dir.resize(2048);
    img.replace(18 * 2048, 2048, dir);
    img.resize(33 * 1024 * 1024);
    return img;
}
QString writeIso(const QString& dir, const QString& name, const QList<QPair<QString, QByteArray>>& files) {
    const QString p = dir + "/" + name;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(syntheticIso(files));
    return p;
}
// (no apostrophe inside this raw string: moc reads one as the start of a character literal and then finds no class)
const char* kManifest = R"JSON({
  "schema": 2, "id": "EXMP-00001", "name": "Example Game", "product": "examplegame", "app_data_folder": "ExampleGame", "binary_name": "examplegame",
  "description": "An example game running natively on your computer, made from the code on your own disc. Nothing from the game is in this application.",
  "disc": { "fingerprint": { "file": "EXMP_000.01", "region": "NTSC-U", "region_label": "North America", "id": "EXMP-00001",
                             "sha256": "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824" },
            "alternates": [ { "file": "EXMP_000.02", "region": "PAL", "region_label": "Europe", "id": "EXMP-00002", "supported": false,
                              "reason": "The European release has different code inside it. This version prepares only the North American disc." } ],
            "required_files": ["SYSTEM.CNF"], "extensions": [".iso"] },
  "targets": { "macos-arm64": { "label": "macOS (Apple silicon)", "os": "macos", "arch": "arm64", "binary": "examplegame.app" },
               "macos-x86_64": { "label": "macOS (Intel)", "os": "macos", "arch": "x86_64", "binary": "examplegame.app" },
               "windows-x64": { "label": "Windows (64-bit)", "os": "windows", "arch": "x86_64", "binary": "examplegame.exe" },
               "linux-x86_64": { "label": "Linux (64-bit)", "os": "linux", "arch": "x86_64", "binary": "examplegame" } },
  "unsupported": [ { "label": "A handheld console", "reason": "The graphics work of the game is far heavier than it can carry at full speed." },
                   { "label": "Other regions of the disc", "reason": "Only the North American disc is prepared by this version." } ],
  "game_folder": { "config": "nativeee.ini", "subfolders": { "memcards": { "label": "Memory cards", "default": "memcards" }, "saves": { "label": "Save states", "default": "saves" }, "cache": { "label": "Cache", "default": "cache" } },
                   "portable_note": "Everything the game needs is in this one folder. Move or copy the folder and the game moves with it." },
  "run": { "args": ["--iso", "${iso}", "--config", "${config}"], "resume_args": ["--load-state", "${state}"], "save_states": { "folder": "${saves}/savestates", "extension": ".ps2st" } },
  "settings": { "note": "Written to nativeee.ini in the game folder, the file the game reads.", "groups": [
    { "title": "Picture", "rows": [
      { "key": "video.fullscreen", "label": "Display mode", "kind": "choice", "default": "windowed", "help": "A window, the whole screen, or a frameless window.", "choices": [ {"value": "windowed", "label": "Windowed"}, {"value": "fullscreen", "label": "Full screen"}, {"value": "borderless", "label": "Borderless"} ] },
      { "key": "video.backend", "label": "Graphics", "kind": "choice", "default": "auto", "choices": [ {"value": "auto", "label": "Auto"}, {"value": "metal", "label": "Metal", "platforms": ["macos"]}, {"value": "gl", "label": "OpenGL"} ] },
      { "key": "video.frame_cap", "label": "Frame rate", "kind": "choice", "default": "60", "help": "The game always runs at its normal speed.", "choices": [ {"value": "30"}, {"value": "60"}, {"value": "off", "label": "Off"} ] },
      { "key": "video.vsync", "label": "Vertical sync", "kind": "switch", "default": "off", "locked_by": { "key": "video.frame_cap", "value": "off", "help": "Off while the frame rate is not capped." } } ] },
    { "title": "Sound", "rows": [
      { "key": "audio.interpolation", "label": "Sound quality", "kind": "choice", "default": "linear", "choices": [ {"value": "linear", "label": "Standard"}, {"value": "gaussian", "label": "Smooth"} ] } ] } ] },
  "scan": { "folders": ["~/Downloads"], "removable": false },
  "licences": [ { "label": "Licence", "file": "LICENSE" } ]
})JSON";

const QRegularExpression kCodenames("\\b(crash\\w*|twinsanity|sly|cooper|slus|sles|sces|scus|scps|scka)\\b", QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kDevVocab("\\b(recompil\\w*|shards?|cmake|ninja|clang|toolchain|git\\s+clone|python|compilers?|linkers?|sha-?256|json)\\b", QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kAcronyms("\\b(AOT|ELF)\\b");
const QRegularExpression kPlaceholder("(lorem|\\bTODO\\b|\\bTBD\\b|placeholder|TextLabel)", QRegularExpression::CaseInsensitiveOption);

bool hasEmoji(const QString& s) {
    for (const QChar& c : s) {
        if (c.isHighSurrogate()) return true;   // every emoji is outside the BMP
        if (c.unicode() >= 0x2600 && c.unicode() <= 0x27BF) return true;   // the misc symbols / dingbats block
    }
    return false;
}
QString stripHtml(QString s) { return s.remove(QRegularExpression("<[^>]*>")); }
}  // namespace

class TestLauncherScreens : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    QString shots_;
    QStringList strings_;
    LauncherApp* app_ = nullptr;
    MainWindow* window_ = nullptr;
    QString goodIso_, palIso_;

    void collectStrings(QWidget* root) {
        for (QLabel* l : root->findChildren<QLabel*>()) if (l->isVisible() && !l->text().isEmpty()) strings_ << stripHtml(l->text());
        for (QAbstractButton* b : root->findChildren<QAbstractButton*>()) if (b->isVisible() && !b->text().isEmpty()) strings_ << b->text();
        for (SegmentedControl* s : root->findChildren<SegmentedControl*>()) if (s->isVisible()) strings_ << s->values();
    }
    void shot(const QString& name) {
        QCoreApplication::processEvents();
        QTest::qWait(60);
        QCOMPARE(window_->size(), QSize(1024, 640));
        const QString path = shots_ + "/" + name + ".png";
        QVERIFY2(!window_->screenshot(path).isEmpty() && QFileInfo::exists(path), qPrintable(path));
        strings_ << QStringLiteral("## %1").arg(name);
        collectStrings(window_);
    }
    void lintStrings() {
        for (const QString& s : strings_) {
            if (s.startsWith("## ")) continue;
            QVERIFY2(!kCodenames.match(s).hasMatch(), qPrintable("codename in: " + s));
            QVERIFY2(!kDevVocab.match(s).hasMatch(), qPrintable("developer vocabulary in: " + s));
            QVERIFY2(!kAcronyms.match(s).hasMatch(), qPrintable("developer acronym in: " + s));
            QVERIFY2(!kPlaceholder.match(s).hasMatch(), qPrintable("placeholder in: " + s));
            QVERIFY2(!hasEmoji(s), qPrintable("emoji in: " + s));
        }
    }
    LauncherApp* makeApp(const QString& home) {
        QDir().mkpath(home);
        qputenv("HOME", home.toUtf8());
        qputenv("USERPROFILE", home.toUtf8());
        qputenv("LOCALAPPDATA", home.toUtf8());
        QFile f(home + "/title.json");
        if (!f.open(QIODevice::WriteOnly)) return nullptr;
        f.write(kManifest);
        f.close();
        auto* app = new LauncherApp(this);
        QString err;
        if (!app->loadManifest(home + "/title.json", &err)) { qWarning("%s", qPrintable(err)); return nullptr; }
        return app;
    }

private slots:
    void initTestCase() {
        shots_ = qEnvironmentVariable("PS2R_LAUNCHER_SCREENS_DIR");
        if (shots_.isEmpty()) shots_ = QDir::currentPath() + "/launcher_screens";
        QDir().mkpath(shots_);
        qputenv("PS2R_LAUNCHER_FAKE_PREPARE", "ok:1.2");
        LauncherStyle::apply();
        app_ = makeApp(home_.path() + "/one");
        QVERIFY(app_);
        window_ = new MainWindow(app_);
        window_->resize(1024, 640);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        QCOMPARE(window_->minimumSize(), QSize(1024, 640));
    }
    void cleanupTestCase() {
        QFile f(shots_ + "/strings.txt");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        for (const QString& s : strings_) f.write((s + '\n').toUtf8());
        delete window_;
        lintStrings();
    }

    void firstRunWhereIsTheGame() {
        QVERIFY(window_->startFirstRunIfNeeded());
        QVERIFY(window_->firstRunShowing());
        FirstRunFlow* flow = window_->firstRun();
        QCOMPARE(flow->currentPage(), FirstRunFlow::WhereIsTheGame);
        QVERIFY(flow->discList()->emptyState()->isVisible());
        QVERIFY(!flow->nextButton()->isEnabled());
        shot("01_first_run_where_is_the_game_empty");
        // a file that is not a disc: the error state in words, nothing refused silently
        QFile junk(home_.path() + "/junk.iso");
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(40 * 1024 * 1024, 'x'));
        junk.close();
        flow->discList()->addDiscFromPath(home_.path() + "/junk.iso");
        QVERIFY(flow->discList()->emptyState()->isVisible());
        QVERIFY(flow->discList()->cards().isEmpty());
        shot("02_first_run_not_a_disc_error");
        // the wrong game: the error names the game and what is missing
        const QString other = writeIso(home_.path(), "other.iso", {{"SYSTEM.CNF", "x"}, {"OTHR_000.01", "hello"}});
        flow->discList()->addDiscFromPath(other);
        QVERIFY(flow->discList()->cards().isEmpty());
        shot("03_first_run_not_the_game_error");
        // one card per image: the other-region disc badged, the modified one badged, the supported one selectable
        palIso_ = writeIso(home_.path(), "europe.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.02", "hello"}});
        const QString modified = writeIso(home_.path(), "changed.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hellp"}});
        goodIso_ = writeIso(home_.path(), "mine.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hello"}});
        flow->discList()->addDiscFromPath(palIso_);
        flow->discList()->addDiscFromPath(modified);
        flow->discList()->addDiscFromPath(goodIso_);
        QCOMPARE(flow->discList()->cards().size(), 3);
        QVERIFY(!flow->discList()->emptyState()->isVisible());
        GameCard* good = flow->discList()->card(goodIso_);
        GameCard* pal = flow->discList()->card(palIso_);
        GameCard* mod = flow->discList()->card(modified);
        QVERIFY(good && pal && mod);
        QVERIFY(good->selected());
        QVERIFY(good->badgeText().isEmpty());
        QCOMPARE(pal->badgeText(), QStringLiteral("Not supported yet"));
        QVERIFY(pal->statusText().contains("European"));
        QCOMPARE(mod->badgeText(), QStringLiteral("Not supported yet"));
        QVERIFY(mod->statusText().contains("differs"));
        QCOMPARE(flow->discList()->cards().first(), good);   // supported first
        QVERIFY(flow->nextButton()->isEnabled());
        shot("04_first_run_discs_found");
    }

    void firstRunWhereItLives() {
        FirstRunFlow* flow = window_->firstRun();
        flow->next();
        QCOMPARE(flow->currentPage(), FirstRunFlow::WhereItLives);
        QVERIFY(flow->folderRow()->path().endsWith("/Example Game"));
        QVERIFY(!app_->gameFolder()->chosen());   // suggested, not yet chosen
        shot("05_first_run_where_it_lives");
        flow->next();
        QVERIFY(app_->gameFolder()->chosen());
        QVERIFY(app_->gameFolder()->path().endsWith("/Example Game"));
        QCOMPARE(flow->currentPage(), FirstRunFlow::WhichComputer);
    }

    void firstRunWhichComputer() {
        FirstRunFlow* flow = window_->firstRun();
        QCOMPARE(flow->targetSelector()->values().size(), 4);
        QCOMPARE(flow->selectedTarget(), app_->hostTarget());
        QCOMPARE(flow->nextButton()->text(), QStringLiteral("Prepare"));
        shot("06_first_run_which_computer");
        flow->targetSelector()->setCurrentValue(app_->hostTarget() == "windows-x64" ? "linux-x86_64" : "windows-x64");
        shot("07_first_run_another_computer");
        flow->targetSelector()->setCurrentValue(app_->hostTarget());
    }

    void firstRunPreparing() {
        FirstRunFlow* flow = window_->firstRun();
        flow->next();
        QCOMPARE(flow->currentPage(), FirstRunFlow::Preparing);
        QVERIFY(app_->preparing());
        QVERIFY(flow->cancelButton()->isVisible());
        QVERIFY(!flow->nextButton()->isVisible());
        QTest::qWait(500);
        QVERIFY(flow->progress()->isVisible());
        QVERIFY(!flow->progress()->text().isEmpty());
        QVERIFY(flow->progress()->caption().contains("%"));
        shot("08_first_run_preparing");
        QTRY_COMPARE_WITH_TIMEOUT(flow->currentPage(), FirstRunFlow::Ready, 5000);
        QVERIFY(app_->gameFolder()->prepared().ok);
        QVERIFY(app_->setupComplete());
        QVERIFY(flow->playButton()->isVisible());
        shot("09_first_run_ready");
        QCOMPARE(flow->nextButton()->text(), QStringLiteral("Done"));
        flow->next();
        QVERIFY(!window_->firstRunShowing());
    }

    void gamesPage() {
        window_->showPage(MainWindow::GamesIndex);
        GamesPage* g = window_->gamesPage();
        QCOMPARE(g->discs()->cards().size(), 3);
        GameCard* good = g->discs()->card(goodIso_);
        QVERIFY(good);
        QCOMPARE(good->actionButton()->text(), QStringLiteral("Play"));
        QCOMPARE(good->statusText(), QStringLiteral("Ready to play."));
        QVERIFY(good->factsText().contains("Prepared"));
        QVERIFY(!g->discs()->card(palIso_)->actionButton()->isVisible());
        QCOMPARE(window_->sidebar()->pageName(1), QStringLiteral("Save States"));
        shot("10_games");
        // the disc gone: the card says so and offers Remove
        QVERIFY(QFile::remove(palIso_));
        app_->library()->refreshMissing();
        g->discs()->refreshCards();
        QCOMPARE(g->discs()->card(palIso_)->actionButton()->text(), QStringLiteral("Remove"));
        g->runCardAction(palIso_);
        QCOMPARE(g->discs()->cards().size(), 2);
    }

    void saveStatesPage() {
        window_->showPage(MainWindow::SaveStatesIndex);
        SaveStatesPage* s = window_->saveStatesPage();
        QVERIFY(s->emptyState()->isVisible());
        QVERIFY(s->cards().isEmpty());
        shot("11_save_states_empty");
        const QString dir = app_->gameFolder()->saveStatesFolder();
        QDir().mkpath(dir);
        for (const char* name : {"slot0", "slot1"}) {
            QFile f(dir + "/" + name + ".ps2st");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(4096, 'a'));
            f.close();
        }
        QFile m(dir + "/slot0.ps2st.meta");
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.write("frame=1501\nwall_ms=1792517610563\n");
        m.close();
        QPixmap thumb(256, 192);
        thumb.fill(QColor("#4C6EF5"));
        QVERIFY(thumb.save(dir + "/slot1.ps2st.png"));
        s->refresh();
        QCOMPARE(s->cards().size(), 2);
        QVERIFY(!s->emptyState()->isVisible());
        QVERIFY(s->cards()[0]->factsText().contains("frame 1501") || s->cards()[1]->factsText().contains("frame 1501"));
        shot("12_save_states");
    }

    void settingsRoundTrip() {
        window_->showPage(MainWindow::SettingsIndex);
        SettingsPage* s = window_->settingsPage();
        QVERIFY(!s->emptyState()->isVisible());
        QVERIFY(s->row("video.frame_cap"));
        QVERIFY(s->row("video.vsync"));
        QVERIFY(s->folderRow("game"));
        QVERIFY(s->folderRow("saves"));
        QCOMPARE(s->folderRow("saves")->path(), app_->gameFolder()->subfolder("saves"));
        // the platform-filtered choice: Metal only on macOS
        const QStringList graphics = s->row("video.backend")->segmented()->values();
        QCOMPARE(graphics.contains("metal"), QString::fromLatin1(
#ifdef Q_OS_MACOS
                                                 "yes"
#else
                                                 "no"
#endif
                                                 ) == "yes");
        QCOMPARE(s->row("video.frame_cap")->value(), QStringLiteral("60"));
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("30"), true);
        IniSettings ini(app_->settingsFilePath());
        QCOMPARE(ini.get("video.frame_cap"), QStringLiteral("30"));
        QVERIFY(ini.path().endsWith("/Example Game/nativeee.ini"));
        shot("13_settings");
        // the lock rule from the manifest: frame rate Off locks vertical sync and says why
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("off"), true);
        QVERIFY(!s->row("video.vsync")->toggle()->isEnabled());
        shot("14_settings_locked_row");
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("60"), true);
        QVERIFY(s->row("video.vsync")->toggle()->isEnabled());
    }

    void aboutAndSheets() {
        window_->showPage(MainWindow::AboutIndex);
        AboutPage* a = window_->aboutPage();
        QVERIFY(a->unsupportedText().contains("handheld"));
        QVERIFY(a->unsupportedText().contains("Other regions"));
        shot("15_about");
        a->removeButton()->click();
        QTest::qWait(50);
        shot("16_about_forget_sheet");
        QTest::keyClick(window_, Qt::Key_Escape);
        QTest::qWait(50);
        window_->detailsSheet()->showLog(QStringLiteral("Details"), QStringLiteral("line one\nline two\n"));
        QVERIFY(window_->detailsSheet()->isOpen());
        shot("17_details_sheet");
        window_->detailsSheet()->closeSheet();
        QVERIFY(!window_->detailsSheet()->isOpen());
    }

    void darkFollowsTheme() {
        LauncherTheme::instance().setDark(true);
        QTest::qWait(50);
        window_->showPage(MainWindow::GamesIndex);
        shot("18_games_dark");
        window_->showPage(MainWindow::SettingsIndex);
        shot("19_settings_dark");
        LauncherTheme::instance().setDark(false);
        QTest::qWait(50);
    }

    void prepareFailureAndNoFolder() {
        // a second launcher: the prepare fails half way; Settings and Save States without a game folder
        delete window_;
        window_ = nullptr;
        qputenv("PS2R_LAUNCHER_FAKE_PREPARE", "fail:0.5:1");
        LauncherApp* app2 = makeApp(home_.path() + "/two");
        QVERIFY(app2);
        window_ = new MainWindow(app2);
        window_->resize(1024, 640);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        window_->showPages();
        window_->showPage(MainWindow::SettingsIndex);
        QVERIFY(window_->settingsPage()->emptyState()->isVisible());
        shot("20_settings_no_folder");
        window_->showPage(MainWindow::SaveStatesIndex);
        QVERIFY(window_->saveStatesPage()->emptyState()->isVisible());
        shot("21_save_states_no_folder");
        window_->showPage(MainWindow::GamesIndex);
        QVERIFY(window_->gamesPage()->discs()->emptyState()->isVisible());
        shot("22_games_empty");
        FirstRunFlow* flow = window_->firstRun();
        QVERIFY(window_->startFirstRunIfNeeded());
        flow->discList()->addDiscFromPath(goodIso_);
        flow->next();
        flow->next();
        QCOMPARE(flow->currentPage(), FirstRunFlow::WhichComputer);
        flow->next();
        QTRY_VERIFY_WITH_TIMEOUT(!flow->errorText().isEmpty(), 5000);
        QCOMPARE(flow->currentPage(), FirstRunFlow::Preparing);
        QVERIFY(flow->retryButton()->isVisible());
        QVERIFY(flow->detailsButton()->isVisible());
        QVERIFY(!flow->progress()->isVisible());
        QVERIFY(flow->errorText().contains("could not be prepared"));
        shot("23_first_run_preparing_failed");
        flow->detailsButton()->click();
        QTest::qWait(50);
        QVERIFY(window_->detailsSheet()->isOpen());
        QVERIFY(window_->detailsSheet()->text().contains("simulated"));
        shot("24_first_run_failure_details");
        window_->detailsSheet()->closeSheet();
        QVERIFY(!app2->setupComplete());
        // the card after a failure: Prepare again, the first run not complete
        QCOMPARE(app2->nextAction(goodIso_), LauncherApp::Prepare);
        // without any runner bundled: the honest state, no fake success
        qunsetenv("PS2R_LAUNCHER_FAKE_PREPARE");
        LauncherApp* app3 = makeApp(home_.path() + "/three");
        QVERIFY(app3);
        QString why;
        QVERIFY(!app3->preparer()->available(app3->hostTarget(), &why));
        QVERIFY(why.contains("cannot prepare"));
    }
};

QTEST_MAIN(TestLauncherScreens)
#include "test_launcher_screens.moc"
