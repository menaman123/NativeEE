// tests/app/launcher/test_launcher_screens.cpp -- the window offscreen at the minimum 1024x640 with a neutral manifest,
// an empty HOME and the fake prepare runner: the setup window in every state (empty, not a disc, not the game, an
// unsupported disc, the supported disc chosen, Advanced, building with the one progress bar, done with Play, failed
// with Try again and Details, dark) and every page after setup (Games, Save States empty and with a state, Settings
// with its lock rule and without a folder, About, the Forget sheet, dark), a PNG of each written to
// PS2R_LAUNCHER_SCREENS_DIR, and no codename / developer vocabulary / placeholder / emoji in any visible string (the
// strings are also dumped to strings.txt for tools/launcher_lint.py).
#include <QtTest/QtTest>
#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include "about_page.h"
#include "app_data.h"
#include "details_sheet.h"
#include "disc_list.h"
#include "empty_state.h"
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
#include "setup_page.h"
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
  "game_folder": { "config": "nativeee.ini", "default": "~/Documents/Example Game", "subfolders": { "memcards": { "label": "Memory cards", "default": "memcards", "key": "paths.mc_dir", "help": "The saved progress of your game. Inside the game folder unless you choose somewhere else." }, "saves": { "label": "Save states", "default": "saves", "key": "paths.save_dir", "help": "Snapshots you can resume from." }, "cache": { "label": "Cache", "default": "cache", "key": "paths.user_data", "ask": false } },
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

    void setupEmpty() {
        // the first run: ONE setup window (decision D-L10); nothing is looked for, the player chooses the disc
        QVERIFY(window_->startSetupIfNeeded());
        QVERIFY(window_->setupShowing());
        SetupPage* setup = window_->setup();
        QCOMPARE(setup->state(), SetupPage::Idle);
        QVERIFY(setup->discPath().isEmpty());
        QVERIFY(setup->discStatus()->isVisible());
        QVERIFY(setup->discStatus()->text().contains("No disc image chosen"));
        QVERIFY(!setup->discCard());
        QVERIFY(setup->buildButton()->isVisible());
        QVERIFY(!setup->buildButton()->isEnabled());
        QCOMPARE(setup->buildButton()->text(), QStringLiteral("Build the game"));
        QVERIFY(setup->buildHint()->text().contains("Choose your disc"));
        QVERIFY(setup->folderRow()->path().endsWith("/Documents/Example Game"));   // suggested, not yet chosen
        QVERIFY(!app_->gameFolder()->chosen());
        // the memory cards and the save states under the game folder: the relative default, following the folder; the
        // cache is not a player's choice and is not here
        QVERIFY(setup->subfolderRow("memcards"));
        QVERIFY(setup->subfolderRow("saves"));
        QVERIFY(!setup->subfolderRow("cache"));
        QCOMPARE(setup->subfolderRow("memcards")->shownText(), QStringLiteral("memcards"));
        QCOMPARE(setup->subfolderRow("saves")->shownText(), QStringLiteral("saves"));
        QCOMPARE(setup->subfolderRow("memcards")->path(), setup->folderPath() + "/memcards");
        QVERIFY(setup->folderNote()->text().contains("Move or copy the folder"));
        QVERIFY(!setup->folderNote()->text().contains("outside"));
        QVERIFY(setup->findChild<QLabel*>("setup_folder_memcards_help")->text().contains("Inside the game folder"));
        QVERIFY(setup->settingsRow("video.frame_cap"));
        QVERIFY(setup->settingsRow("video.vsync"));
        QVERIFY(!setup->advancedOpen());
        QVERIFY(!setup->targetSelector()->isVisible());
        QVERIFY(!setup->progress()->isVisible());
        QVERIFY(!setup->stopButton()->isVisible());
        QVERIFY(setup->leaveButton()->isVisible());
        shot("01_setup_empty");
        // a file that is not a disc: the reason in words, the Build button stays off
        QFile junk(home_.path() + "/junk.iso");
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(40 * 1024 * 1024, 'x'));
        junk.close();
        setup->chooseDisc(home_.path() + "/junk.iso");
        QVERIFY(setup->discStatus()->isVisible());
        QVERIFY(!setup->discCard());
        QVERIFY(!setup->buildButton()->isEnabled());
        QVERIFY(setup->discField()->text().endsWith("junk.iso"));
        shot("02_setup_not_a_disc");
        // the wrong game: the error names the game and what is missing
        const QString other = writeIso(home_.path(), "other.iso", {{"SYSTEM.CNF", "x"}, {"OTHR_000.01", "hello"}});
        setup->chooseDisc(other);
        QVERIFY(!setup->discCard());
        QVERIFY(setup->discStatus()->text().contains("Example Game"));
        QVERIFY(!setup->buildButton()->isEnabled());
        shot("03_setup_not_the_game");
    }

    void setupDiscChosen() {
        SetupPage* setup = window_->setup();
        // another region: the card with the badge and the plain reason; Build stays off and says why
        palIso_ = writeIso(home_.path(), "europe.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.02", "hello"}});
        setup->chooseDisc(palIso_);
        QVERIFY(setup->discCard());
        QCOMPARE(setup->discCard()->badgeText(), QStringLiteral("Not supported yet"));
        QVERIFY(setup->discCard()->statusText().contains("European"));
        QVERIFY(!setup->discCard()->selected());
        QVERIFY(!setup->buildButton()->isEnabled());
        QVERIFY(setup->buildHint()->text().contains("European"));
        shot("04_setup_unsupported_disc");
        // a modified image: recognised, badged
        const QString modified = writeIso(home_.path(), "changed.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hellp"}});
        setup->chooseDisc(modified);
        QCOMPARE(setup->discCard()->badgeText(), QStringLiteral("Not supported yet"));
        QVERIFY(setup->discCard()->statusText().contains("differs"));
        QVERIFY(!setup->buildButton()->isEnabled());
        // the supported disc: the card, selected, no badge; Build on
        goodIso_ = writeIso(home_.path(), "mine.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hello"}});
        setup->chooseDisc(goodIso_);
        QVERIFY(setup->discCard());
        QVERIFY(setup->discCard()->badgeText().isEmpty());
        QVERIFY(setup->discCard()->selected());
        QVERIFY(setup->discCard()->statusText().contains("Build the game"));
        QVERIFY(setup->buildButton()->isEnabled());
        QVERIFY(!setup->buildHint()->isVisible());
        QCOMPARE(app_->currentDisc(), goodIso_);
        QCOMPARE(app_->library()->games().size(), 3);
        // the settings rows: a change is kept until the game is built (no folder yet); the lock rule holds here too
        SettingsRow* cap = setup->settingsRow("video.frame_cap");
        cap->segmented()->setCurrentIndex(cap->segmented()->values().indexOf("30"), true);
        QCOMPARE(setup->pendingSettings().value("video.frame_cap"), QStringLiteral("30"));
        QVERIFY(!app_->gameFolder()->chosen());
        cap->segmented()->setCurrentIndex(cap->segmented()->values().indexOf("off"), true);
        QVERIFY(!setup->settingsRow("video.vsync")->toggle()->isEnabled());
        cap->segmented()->setCurrentIndex(cap->segmented()->values().indexOf("30"), true);
        QVERIFY(setup->settingsRow("video.vsync")->toggle()->isEnabled());
        const QStringList graphics = setup->settingsRow("video.backend")->segmented()->values();
        QCOMPARE(graphics.contains("metal"), QString::fromLatin1(
#ifdef Q_OS_MACOS
                                                 "yes"
#else
                                                 "no"
#endif
                                                 ) == "yes");
        shot("05_setup_disc_chosen");
        // the folders chosen elsewhere (no game folder yet): the save states outside show the absolute path and the
        // note says so; the memory cards inside the suggested folder stay relative and follow it
        setup->chooseSubfolder("saves", home_.path() + "/My save states");
        QCOMPARE(setup->subfolderRow("saves")->shownText(), AppData::shortPath(home_.path() + "/My save states"));
        QCOMPARE(setup->subfolderRow("saves")->path(), home_.path() + "/My save states");
        QVERIFY(setup->folderNote()->text().contains("The save states live outside the game folder; moving the folder leaves them where they are."));
        QVERIFY(!setup->folderNote()->text().contains("Move or copy the folder"));
        setup->chooseSubfolder("memcards", setup->folderPath() + "/cards");
        QCOMPARE(setup->subfolderRow("memcards")->shownText(), QStringLiteral("cards"));
        QVERIFY(!setup->folderNote()->text().contains("memory cards live outside"));
        QVERIFY(!app_->gameFolder()->chosen());
        QVERIFY(!QFileInfo::exists(setup->folderPath() + "/nativeee.ini"));   // nothing written before Build
        setup->scrollToFolders();
        shot("05b_setup_folders_custom");
        setup->chooseSubfolder("memcards", setup->folderPath() + "/memcards");   // back to the default
        QCOMPARE(setup->subfolderRow("memcards")->shownText(), QStringLiteral("memcards"));
    }

    void setupAdvanced() {
        SetupPage* setup = window_->setup();
        setup->setAdvancedOpen(true);
        QVERIFY(setup->targetSelector()->isVisible());
        QCOMPARE(setup->targetSelector()->values().size(), 4);
        QCOMPARE(setup->selectedTarget(), app_->hostTarget());
        QVERIFY(setup->targetNote()->text().contains("This computer"));
        shot("06_setup_advanced");
        const QString other = app_->hostTarget() == "windows-x64" ? "linux-x86_64" : "windows-x64";
        setup->targetSelector()->setCurrentIndex(setup->targetSelector()->values().indexOf(other), true);   // a click
        QCOMPARE(setup->selectedTarget(), other);
        QVERIFY(setup->targetNote()->text().contains("Another computer"));
        shot("06b_setup_another_computer");
        setup->targetSelector()->setCurrentIndex(setup->targetSelector()->values().indexOf(app_->hostTarget()), true);
        QCOMPARE(setup->selectedTarget(), app_->hostTarget());
        setup->setAdvancedOpen(false);
        QVERIFY(!setup->targetSelector()->isVisible());
    }

    void setupBuilding() {
        SetupPage* setup = window_->setup();
        setup->build();
        QCOMPARE(setup->state(), SetupPage::Building);
        QVERIFY(app_->preparing());
        // the folder was chosen when Build was pressed and the kept settings written into it
        QVERIFY(app_->gameFolder()->chosen());
        QVERIFY(app_->gameFolder()->path().endsWith("/Documents/Example Game"));
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("video.frame_cap"), QStringLiteral("30"));
        QVERIFY(setup->pendingSettings().isEmpty());
        // ...and the folder choices as the keys the game reads: the memory cards inside (absolute in the file), the
        // save states where they were chosen
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.mc_dir"), app_->gameFolder()->path() + "/memcards");
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.save_dir"), home_.path() + "/My save states");
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.user_data"), app_->gameFolder()->path() + "/cache");
        QVERIFY(QDir(home_.path() + "/My save states").exists());
        QVERIFY(setup->stopButton()->isVisible());
        QVERIFY(setup->detailsButton()->isVisible());
        QVERIFY(!setup->buildButton()->isVisible());
        QVERIFY(!setup->discField()->isEnabled());
        QTest::qWait(500);
        QVERIFY(setup->progress()->isVisible());
        QVERIFY(!setup->progress()->text().isEmpty());
        QVERIFY(setup->progress()->caption().contains("%"));
        shot("07_setup_building");
        QTRY_COMPARE_WITH_TIMEOUT(setup->state(), SetupPage::Done, 5000);
        QVERIFY(app_->gameFolder()->prepared().ok);
        QVERIFY(app_->setupComplete());
        QVERIFY(setup->playButton()->isVisible());
        QVERIFY(!setup->stopButton()->isVisible());
        QCOMPARE(setup->progress()->text(), QStringLiteral("Your game is ready"));
        QCOMPARE(setup->leaveButton()->text(), QStringLiteral("Go to the launcher"));
        QCOMPARE(setup->discCard()->statusText(), QStringLiteral("Ready to play."));
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("video.frame_cap"), QStringLiteral("30"));   // written again after the build
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.save_dir"), home_.path() + "/My save states");   // and the folders (D-L13)
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.mc_dir"), app_->gameFolder()->path() + "/memcards");
        QCOMPARE(app_->gameFolder()->saveStatesFolder(), home_.path() + "/My save states/savestates");
        shot("08_setup_done");
        setup->leave();
        QVERIFY(!window_->setupShowing());
        // a returning player lands on the pages, never on the setup window
        QVERIFY(!window_->startSetupIfNeeded());
        QVERIFY(!window_->setupShowing());
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
        QCOMPARE(g->discs()->addButton()->text(), QStringLiteral("Add a game…"));
        shot("09_games");
        // "Add a game…" opens the setup window again, empty; the pages come back
        g->discs()->addButton()->click();
        QVERIFY(window_->setupShowing());
        QCOMPARE(window_->setup()->state(), SetupPage::Idle);
        QVERIFY(window_->setup()->discPath().isEmpty());
        QVERIFY(window_->setup()->folderRow()->path().endsWith("/Documents/Example Game"));
        window_->setup()->leave();
        QVERIFY(!window_->setupShowing());
        QCOMPARE(window_->currentPage(), int(MainWindow::GamesIndex));
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
        shot("10_save_states_empty");
        const QString dir = app_->gameFolder()->saveStatesFolder();
        QCOMPARE(dir, home_.path() + "/My save states/savestates");   // the Save States page lists the folder chosen on the setup window
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
        // the long path of a folder chosen elsewhere wraps; the page never grows wider than the window
        QVERIFY(!s->findChild<QScrollArea*>()->horizontalScrollBar()->isVisible());
        shot("11_save_states");
    }

    void settingsRoundTrip() {
        window_->showPage(MainWindow::SettingsIndex);
        SettingsPage* s = window_->settingsPage();
        QVERIFY(!s->emptyState()->isVisible());
        QVERIFY(s->row("video.frame_cap"));
        QVERIFY(s->row("video.vsync"));
        QVERIFY(s->folderRow("game"));
        QVERIFY(s->folderRow("saves"));
        QVERIFY(s->folderRow("memcards"));
        QVERIFY(s->folderRow("cache"));   // the cache: here, not on the setup window
        // the same store as the setup window: the save states chosen there, the memory cards inside shown relative
        QCOMPARE(s->folderRow("saves")->path(), app_->gameFolder()->subfolder("saves"));
        QCOMPARE(s->folderRow("saves")->path(), home_.path() + "/My save states");
        QCOMPARE(s->folderRow("memcards")->shownText(), QStringLiteral("memcards"));
        QCOMPARE(s->folderRow("cache")->shownText(), QStringLiteral("cache"));
        // a change here is a change in the file and on the setup window
        app_->setSubfolder("memcards", home_.path() + "/My cards");
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("paths.mc_dir"), home_.path() + "/My cards");
        QCOMPARE(s->folderRow("memcards")->shownText(), AppData::shortPath(home_.path() + "/My cards"));
        QCOMPARE(window_->setup()->subfolderRow("memcards")->path(), home_.path() + "/My cards");
        app_->setSubfolder("memcards", app_->gameFolder()->path() + "/memcards");
        QCOMPARE(s->folderRow("memcards")->shownText(), QStringLiteral("memcards"));
        // the platform-filtered choice: Metal only on macOS
        const QStringList graphics = s->row("video.backend")->segmented()->values();
        QCOMPARE(graphics.contains("metal"), QString::fromLatin1(
#ifdef Q_OS_MACOS
                                                 "yes"
#else
                                                 "no"
#endif
                                                 ) == "yes");
        QCOMPARE(s->row("video.frame_cap")->value(), QStringLiteral("30"));   // the setup window's choice, from the file
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("60"), true);
        IniSettings ini(app_->settingsFilePath());
        QCOMPARE(ini.get("video.frame_cap"), QStringLiteral("60"));
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("30"), true);
        QCOMPARE(IniSettings(app_->settingsFilePath()).get("video.frame_cap"), QStringLiteral("30"));
        QVERIFY(ini.path().endsWith("/Example Game/nativeee.ini"));
        shot("12_settings");
        // the lock rule from the manifest: frame rate Off locks vertical sync and says why
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("off"), true);
        QVERIFY(!s->row("video.vsync")->toggle()->isEnabled());
        shot("13_settings_locked_row");
        s->row("video.frame_cap")->segmented()->setCurrentIndex(s->row("video.frame_cap")->segmented()->values().indexOf("60"), true);
        QVERIFY(s->row("video.vsync")->toggle()->isEnabled());
        // the game folder's Change… on Settings opens the setup window on the current disc
        s->folderRow("game")->changeButton()->click();
        QVERIFY(window_->setupShowing());
        QCOMPARE(window_->setup()->discPath(), goodIso_);
        QVERIFY(window_->setup()->buildButton()->isEnabled());
        window_->setup()->leave();
        QVERIFY(!window_->setupShowing());
    }

    void aboutAndSheets() {
        window_->showPage(MainWindow::AboutIndex);
        AboutPage* a = window_->aboutPage();
        QVERIFY(a->unsupportedText().contains("handheld"));
        QVERIFY(a->unsupportedText().contains("Other regions"));
        shot("14_about");
        a->removeButton()->click();
        QTest::qWait(50);
        shot("15_about_forget_sheet");
        QTest::keyClick(window_, Qt::Key_Escape);
        QTest::qWait(50);
        window_->detailsSheet()->showLog(QStringLiteral("Details"), QStringLiteral("line one\nline two\n"));
        QVERIFY(window_->detailsSheet()->isOpen());
        shot("16_details_sheet");
        window_->detailsSheet()->closeSheet();
        QVERIFY(!window_->detailsSheet()->isOpen());
    }

    void darkFollowsTheme() {
        LauncherTheme::instance().setDark(true);
        QTest::qWait(50);
        window_->showPage(MainWindow::GamesIndex);
        shot("17_games_dark");
        window_->showPage(MainWindow::SettingsIndex);
        shot("18_settings_dark");
        window_->showSetup(goodIso_);
        QTest::qWait(50);
        shot("24_setup_dark");
        window_->setup()->leave();
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
        shot("19_settings_no_folder");
        window_->showPage(MainWindow::SaveStatesIndex);
        QVERIFY(window_->saveStatesPage()->emptyState()->isVisible());
        shot("20_save_states_no_folder");
        window_->showPage(MainWindow::GamesIndex);
        QVERIFY(window_->gamesPage()->discs()->emptyState()->isVisible());
        shot("21_games_empty");
        SetupPage* setup = window_->setup();
        QVERIFY(window_->startSetupIfNeeded());
        setup->chooseDisc(goodIso_);
        QVERIFY(setup->buildButton()->isEnabled());
        setup->build();
        QCOMPARE(setup->state(), SetupPage::Building);
        QTRY_VERIFY_WITH_TIMEOUT(!setup->errorText().isEmpty(), 5000);
        QCOMPARE(setup->state(), SetupPage::Failed);
        QVERIFY(setup->retryButton()->isVisible());
        QVERIFY(setup->detailsButton()->isVisible());
        QVERIFY(!setup->progress()->isVisible());
        QVERIFY(!setup->buildButton()->isVisible());
        QVERIFY(setup->discField()->isEnabled());   // the inputs are back
        QVERIFY(setup->errorText().contains("could not be prepared"));
        shot("22_setup_failed");
        setup->detailsButton()->click();
        QTest::qWait(50);
        QVERIFY(window_->detailsSheet()->isOpen());
        QVERIFY(window_->detailsSheet()->text().contains("simulated"));
        shot("23_setup_failure_details");
        window_->detailsSheet()->closeSheet();
        QVERIFY(!app2->setupComplete());
        // the card after a failure: build again, the setup not complete
        QCOMPARE(app2->nextAction(goodIso_), LauncherApp::Prepare);
        // Try again is the same Build
        setup->build();
        QCOMPARE(setup->state(), SetupPage::Building);
        setup->stop();
        QTRY_COMPARE_WITH_TIMEOUT(setup->state(), SetupPage::Failed, 5000);
        QVERIFY(setup->errorText().contains("stopped"));
        // without the fake: the shipped pipeline when the launcher was built with it, else the honest "cannot
        // prepare" state -- never a fake success
        qunsetenv("PS2R_LAUNCHER_FAKE_PREPARE");
        LauncherApp* app3 = makeApp(home_.path() + "/three");
        QVERIFY(app3);
        QString why;
#ifdef PS2R_LAUNCHER_HAS_PREPARE
        QVERIFY(app3->preparer()->available(app3->hostTarget(), &why));
        QVERIFY(!app3->preparer()->available(app3->hostTarget() == "windows-x64" ? "linux-x86_64" : "windows-x64", &why));
        QVERIFY(why.contains("only be made on the computer"));
#else
        QVERIFY(!app3->preparer()->available(app3->hostTarget(), &why));
        QVERIFY(why.contains("cannot prepare"));
#endif
    }
};

QTEST_MAIN(TestLauncherScreens)
#include "test_launcher_screens.moc"
