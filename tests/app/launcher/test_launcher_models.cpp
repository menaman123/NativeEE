// tests/app/launcher/test_launcher_models.cpp -- the launcher's models without a window: the manifest (schema 2), the
// ISO 9660 fingerprint and the executable's checksum on a synthetic image, the identity of a supported / other-region
// / modified / wrong disc, the game folder's layout and run arguments, the options-file round trip, the step line, the
// card's relative times, the Build rule (LauncherApp::canPrepare: a chosen, present, supported disc and nothing being
// prepared) and the pipeline's platform id of a manifest target.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "app_data.h"
#include "disc_identifier.h"
#include "game_folder.h"
#include "game_library.h"
#include "generated_artwork.h"
#include "host_platform.h"
#include "ini_settings.h"
#include "iso_fingerprint.h"
#include "launcher_app.h"
#include "prepare_runner.h"
#ifdef PS2R_LAUNCHER_HAS_PREPARE
#include "bundled_prepare_runner.h"
#endif
#include "process_runner.h"
#include "save_state_list.h"
#include "title_manifest.h"
using namespace ps2r::launcher;

namespace {
// A minimal ISO 9660 image: PVD at sector 16, a root directory at sector 18 naming `files`; each file's bytes at its
// own sector from 19 on.
QByteArray syntheticIso(const QList<QPair<QString, QByteArray>>& files) {
    QByteArray img(2048 * (20 + files.size()), '\0');
    QByteArray pvd(2048, '\0');
    pvd[0] = 1;
    pvd.replace(1, 5, "CD001");
    pvd[6] = 1;
    pvd.replace(40, 32, QByteArray("EXAMPLE                         "));
    QByteArray root(34, '\0');
    root[0] = 34;
    root[2] = 18;                 // extent 18
    root[10] = 0; root[11] = 8;   // 2048 little-endian
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
    img.resize(33 * 1024 * 1024);   // the scanner's minimum
    return img;
}
QString writeIso(const QTemporaryDir& dir, const QString& name, const QList<QPair<QString, QByteArray>>& files) {
    const QString p = dir.path() + "/" + name;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(syntheticIso(files));
    return p;
}
const char* kHelloSha = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";   // sha256("hello")
const char* kManifest = R"JSON({
  "schema": 2, "id": "EXMP-00001", "name": "Example Game", "product": "examplegame", "app_data_folder": "ExampleGame", "binary_name": "examplegame",
  "description": "A test description.",
  "disc": { "fingerprint": { "file": "EXMP_000.01", "region": "NTSC-U", "region_label": "North America", "id": "EXMP-00001",
                             "sha256": "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824" },
            "alternates": [ { "file": "EXMP_000.02", "region": "PAL", "region_label": "Europe", "id": "EXMP-00002", "supported": false, "reason": "The European release is planned." } ],
            "required_files": ["SYSTEM.CNF"], "extensions": [".iso"] },
  "release": { "repo": "example/examplegame", "asset_pattern": "${product}-${tag}-${platform}.zip", "estimate": "about 1 to 2 minutes" },
  "targets": { "macos-arm64": { "label": "macOS (Apple silicon)", "os": "macos", "arch": "arm64", "binary": "examplegame.app" },
               "windows-x64": { "label": "Windows", "os": "windows", "arch": "x86_64", "binary": "examplegame.exe" },
               "linux-x86_64": { "label": "Linux", "os": "linux", "arch": "x86_64", "binary": "examplegame" },
               "macos-x86_64": { "label": "macOS (Intel)", "os": "macos", "arch": "x86_64", "binary": "examplegame.app" } },
  "unsupported": [ { "label": "A handheld", "reason": "Too heavy for it." } ],
  "game_folder": { "config": "nativeee.ini", "default": "~/Documents/Example Game", "subfolders": { "memcards": { "label": "Memory cards", "default": "memcards", "key": "paths.mc_dir", "help": "The saved progress of your game. Inside the game folder unless you choose somewhere else." }, "saves": { "label": "Save states", "default": "saves", "key": "paths.save_dir", "help": "Snapshots you can resume from." }, "cache": { "label": "Cache", "default": "cache", "key": "paths.user_data", "ask": false } } },
  "run": { "args": ["--iso", "${iso}", "--config", "${config}", "--set", "paths.save_dir=${saves}"], "resume_args": ["--load-state", "${state}"],
           "save_states": { "folder": "${saves}/savestates", "extension": ".ps2st" } },
  "settings": { "groups": [ { "title": "Picture", "rows": [
    { "key": "video.frame_cap", "label": "Frame rate", "kind": "choice", "default": "60", "choices": [ {"value": "30"}, {"value": "60"}, {"value": "off", "label": "Off"} ] },
    { "key": "video.vsync", "label": "Vertical sync", "kind": "switch", "default": "off", "locked_by": { "key": "video.frame_cap", "value": "off", "help": "Off while the frame rate is not capped." } },
    { "key": "video.backend", "label": "Graphics", "kind": "choice", "default": "auto", "choices": [ {"value": "auto"}, {"value": "metal", "platforms": ["macos"]}, {"value": "gl", "label": "OpenGL"} ] } ] } ] }
})JSON";
}  // namespace

class TestLauncherModels : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    TitleManifest manifest_;

private slots:
    void initTestCase() {
        qputenv("HOME", home_.path().toUtf8());
        qputenv("USERPROFILE", home_.path().toUtf8());
        qputenv("LOCALAPPDATA", home_.path().toUtf8());
        QFile f(home_.path() + "/title.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kManifest);
        f.close();
        QString err;
        QVERIFY2(manifest_.load(home_.path() + "/title.json", &err), qPrintable(err));
    }

    void manifest() {
        QCOMPARE(manifest_.id(), QStringLiteral("EXMP-00001"));
        QCOMPARE(manifest_.binaryName(), QStringLiteral("examplegame"));
        QCOMPARE(manifest_.fingerprint().sha256, QString::fromLatin1(kHelloSha));
        QCOMPARE(manifest_.alternates().size(), 1);
        QVERIFY(!manifest_.alternates()[0].supported);
        QCOMPARE(manifest_.targets().size(), 4);
        QCOMPARE(manifest_.targets()[0].os, QStringLiteral("macos"));   // the stable order: macOS, Windows, Linux
        QCOMPARE(manifest_.targets()[2].id, QStringLiteral("windows-x64"));
        QCOMPARE(manifest_.releaseAsset("v1.2", "macos-arm64"), QStringLiteral("examplegame-v1.2-macos-arm64.zip"));
        QString err;
        QCOMPARE(manifest_.releaseIndexUrl(), QStringLiteral("https://api.github.com/repos/example/examplegame/releases"));
        QCOMPARE(manifest_.prepareEstimate(), QStringLiteral("about 1 to 2 minutes"));
        QCOMPARE(manifest_.defaultGameFolder(), QStringLiteral("~/Documents/Example Game"));
        TitleManifest explicitIndex;
        QVERIFY(explicitIndex.loadJson(QJsonObject{{"id", "X"}, {"name", "X"}, {"disc", QJsonObject{{"fingerprint", QJsonObject{{"file", "X_000.01"}}}}},
                                                   {"release", QJsonObject{{"index_url", "http://127.0.0.1:1/releases.json"}}}}, &err));   // lint: allow
        QCOMPARE(explicitIndex.releaseIndexUrl(), QStringLiteral("http://127.0.0.1:1/releases.json"));   // lint: allow
        TitleManifest noRelease;
        QVERIFY(noRelease.loadJson(QJsonObject{{"id", "X"}, {"name", "X"}, {"disc", QJsonObject{{"fingerprint", QJsonObject{{"file", "X_000.01"}}}}}}, &err));
        QVERIFY(noRelease.releaseIndexUrl().isEmpty());
        QVERIFY(noRelease.defaultGameFolder().isEmpty());
        QCOMPARE(manifest_.subfolders().size(), 3);
        // the sub-folders name the options-file key the game reads; the cache is not asked on the setup window
        QCOMPARE(manifest_.subfolder("memcards")->key, QStringLiteral("paths.mc_dir"));
        QCOMPARE(manifest_.subfolder("saves")->key, QStringLiteral("paths.save_dir"));
        QVERIFY(manifest_.subfolder("memcards")->ask);
        QVERIFY(manifest_.subfolder("saves")->ask);
        QVERIFY(!manifest_.subfolder("cache")->ask);
        QVERIFY(manifest_.subfolder("memcards")->help.contains("Inside the game folder"));
        QVERIFY(!manifest_.subfolder("nothing"));
        QCOMPARE(manifest_.unsupported().size(), 1);
        QCOMPARE(manifest_.allSettingsRows().size(), 3);
        QCOMPARE(manifest_.allSettingsRows()[1].lockedBy.key, QStringLiteral("video.frame_cap"));
        QCOMPARE(manifest_.allSettingsRows()[2].choices[1].platforms, QStringList{"macos"});
        TitleManifest bad;
        QVERIFY(!bad.loadJson(QJsonObject{{"id", "X"}}, &err));
        QVERIFY(!err.isEmpty());
    }

    void fingerprintAndChecksum() {
        const QString iso = writeIso(home_, "good.iso", {{"SYSTEM.CNF", "BOOT2 = cdrom0:\\EXMP_000.01;1"}, {"EXMP_000.01", "hello"}});
        QStringList entries;
        QString err;
        QVERIFY2(IsoFingerprint::rootEntries(iso, &entries, &err), qPrintable(err));
        QVERIFY(entries.contains("EXMP_000.01"));
        QCOMPARE(IsoFingerprint::fileSha256(iso, "EXMP_000.01", &err), QString::fromLatin1(kHelloSha));
        QCOMPARE(IsoFingerprint::volumeId(iso), QStringLiteral("EXAMPLE"));
        QFile junk(home_.path() + "/junk.iso");
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(40 * 1024 * 1024, 'x'));
        junk.close();
        QString matched;
        QCOMPARE(IsoFingerprint::identify(home_.path() + "/junk.iso", {"EXMP_000.01"}, &matched, &err), IsoFingerprint::NotAnIso);
    }

    void identity() {
        const QString good = writeIso(home_, "a.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hello"}});
        const QString pal = writeIso(home_, "b.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.02", "hello"}});
        const QString modified = writeIso(home_, "c.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hellp"}});
        const QString other = writeIso(home_, "d.iso", {{"SYSTEM.CNF", "x"}, {"OTHR_000.01", "hello"}});
        DiscIdentity d = DiscIdentifier::identify(manifest_, good);
        QCOMPARE(d.status, DiscIdentity::Supported);
        QVERIFY(d.playable());
        QCOMPARE(d.sha256, QString::fromLatin1(kHelloSha));
        QCOMPARE(DiscIdentifier::subtitle(d), QStringLiteral("NTSC-U (North America) · EXMP-00001 · a.iso"));
        d = DiscIdentifier::identify(manifest_, pal);
        QCOMPARE(d.status, DiscIdentity::OtherRegion);
        QVERIFY(d.recognised() && !d.playable());
        QCOMPARE(d.reason, QStringLiteral("The European release is planned."));
        d = DiscIdentifier::identify(manifest_, modified);
        QCOMPARE(d.status, DiscIdentity::Modified);
        QVERIFY(d.recognised());
        QVERIFY(d.reason.contains("differs"));
        d = DiscIdentifier::identify(manifest_, other);
        QCOMPARE(d.status, DiscIdentity::NotTheGame);
        QVERIFY(!d.recognised());
        QVERIFY(d.reason.contains("Example Game"));
        QCOMPARE(DiscIdentity::statusFromKey(DiscIdentity::statusKey(DiscIdentity::Modified)), DiscIdentity::Modified);
    }

    void gameFolder() {
        AppData data("ExampleGameModels");
        GameFolder f(&data, &manifest_);
        QVERIFY(!f.chosen());
        QCOMPARE(GameFolder::safeFolderName("Sly: Game/2"), QStringLiteral("Sly Game 2"));
        QCOMPARE(f.folderFor(home_.path()), home_.path() + "/Example Game");
        // the suggested folder: the manifest's default under the HOME the launcher uses, never the real user's
        QCOMPARE(f.suggestedPath(), home_.path() + "/Documents/Example Game");
        QCOMPARE(f.defaultParent(), QDir(home_.path() + "/Documents").exists() ? home_.path() + "/Documents" : home_.path());
        // before any folder is chosen the sub-folders resolve against the suggested one and show their relative form
        QCOMPARE(f.subfolderChoice("memcards"), QStringLiteral("memcards"));
        QVERIFY(f.subfolderInside("memcards"));
        QCOMPARE(f.subfolderIn("memcards", "/g"), QStringLiteral("/g/memcards"));
        QCOMPARE(GameFolder::relativeIfInside("/g/memcards", "/g"), QStringLiteral("memcards"));
        QCOMPARE(GameFolder::relativeIfInside("/g/cards/here", "/g/"), QStringLiteral("cards/here"));
        QCOMPARE(GameFolder::relativeIfInside("/elsewhere/g", "/g"), QStringLiteral("/elsewhere/g"));
        QCOMPARE(GameFolder::resolveIn("memcards", "/g"), QStringLiteral("/g/memcards"));
        QCOMPARE(GameFolder::resolveIn("/v/cards", "/g"), QStringLiteral("/v/cards"));
        // a choice made before the folder exists waits (D-L15): inside the suggested folder -> relative; elsewhere -> absolute
        f.setSubfolder("memcards", f.suggestedPath() + "/cards");
        QCOMPARE(f.subfolderChoice("memcards"), QStringLiteral("cards"));
        f.setSubfolder("saves", home_.path() + "/elsewhere");
        QCOMPARE(f.subfolderChoice("saves"), home_.path() + "/elsewhere");
        QVERIFY(!f.subfolderInside("saves"));
        QVERIFY(!QFileInfo::exists(home_.path() + "/Example Game/nativeee.ini"));   // nothing written yet: no folder
        f.setSubfolder("cache", home_.path() + "/cachehere");   // the cache has a key and is written like the rest; an unknown id is ignored
        f.setSubfolder("nothing", "/nowhere");
        QVERIFY(f.subfolderKey("nothing").isEmpty());
        f.choose(home_.path() + "/Example Game");
        QVERIFY(f.chosen());
        QCOMPARE(f.configPath(), home_.path() + "/Example Game/nativeee.ini");
        // choosing the folder wrote every keyed sub-folder into the options file, as the absolute path the game reads
        IniSettings ini(f.configPath());
        QVERIFY(ini.exists());
        QCOMPARE(ini.get("paths.mc_dir"), home_.path() + "/Example Game/cards");    // relative followed the game folder
        QCOMPARE(ini.get("paths.save_dir"), home_.path() + "/elsewhere");           // absolute stayed
        QCOMPARE(ini.get("paths.user_data"), home_.path() + "/cachehere");
        QCOMPARE(f.subfolder("memcards"), home_.path() + "/Example Game/cards");
        QCOMPARE(f.subfolderChoice("memcards"), QStringLiteral("cards"));   // read back from the file, made relative
        QCOMPARE(f.subfolder("saves"), home_.path() + "/elsewhere");
        QCOMPARE(f.saveStatesFolder(), home_.path() + "/elsewhere/savestates");
        // back inside: relative again, the file says the absolute path
        f.setSubfolder("saves", home_.path() + "/Example Game/saves");
        QCOMPARE(f.subfolderChoice("saves"), QStringLiteral("saves"));
        QCOMPARE(f.subfolder("saves"), home_.path() + "/Example Game/saves");
        QCOMPARE(f.saveStatesFolder(), home_.path() + "/Example Game/saves/savestates");
        QCOMPARE(IniSettings(f.configPath()).get("paths.save_dir"), home_.path() + "/Example Game/saves");
        f.setSubfolder("saves", home_.path() + "/elsewhere");
        QCOMPARE(f.subfolder("saves"), home_.path() + "/elsewhere");
        QCOMPARE(f.runArgs("/d/game.iso"), QStringList({"--iso", "/d/game.iso", "--config", home_.path() + "/Example Game/nativeee.ini", "--set", "paths.save_dir=" + home_.path() + "/elsewhere"}));
        QCOMPARE(f.runArgs("/d/game.iso", "/s/one.ps2st").last(), QStringLiteral("/s/one.ps2st"));
        // the file is the one store: a value the game (or a hand) wrote there is what the launcher shows
        QVERIFY(IniSettings(f.configPath()).set("paths.mc_dir", home_.path() + "/Example Game/memcards"));
        QCOMPARE(f.subfolderChoice("memcards"), QStringLiteral("memcards"));
        QVERIFY(IniSettings(f.configPath()).set("paths.mc_dir", "/v/cards"));
        QCOMPARE(f.subfolder("memcards"), QStringLiteral("/v/cards"));
        QVERIFY(!f.subfolderInside("memcards"));
        // the game folder moves: a relative choice follows, an absolute one stays, the new folder's file gets both
        QVERIFY(IniSettings(f.configPath()).set("paths.mc_dir", home_.path() + "/Example Game/memcards"));
        f.choose(home_.path() + "/Moved Game");
        QCOMPARE(f.subfolder("memcards"), home_.path() + "/Moved Game/memcards");
        QCOMPARE(f.subfolder("saves"), home_.path() + "/elsewhere");
        QCOMPARE(IniSettings(f.configPath()).get("paths.mc_dir"), home_.path() + "/Moved Game/memcards");
        QCOMPARE(IniSettings(f.configPath()).get("paths.save_dir"), home_.path() + "/elsewhere");
        // the pipeline rewrote the file without the save states key and with its own memory cards line (D-L13): the
        // choices kept at Build go back in and the file reads as the launcher shows
        const QMap<QString, QString> atBuild = f.subfolderChoices();
        QCOMPARE(atBuild.value("saves"), home_.path() + "/elsewhere");
        QCOMPARE(atBuild.value("memcards"), QStringLiteral("memcards"));
        QFile rewrite(f.configPath());
        QVERIFY(rewrite.open(QIODevice::WriteOnly | QIODevice::Truncate));
        rewrite.write("# written by the pipeline\npaths.user_data = /p/cache\npaths.mc_dir = /p/memcards\npaths.iso = /d/game.iso\n");
        rewrite.close();
        QCOMPARE(f.subfolder("memcards"), QStringLiteral("/p/memcards"));
        QCOMPARE(f.subfolder("saves"), home_.path() + "/Moved Game/saves");   // the key is gone: the default
        f.setSubfolderChoices(atBuild);
        QCOMPARE(IniSettings(f.configPath()).get("paths.mc_dir"), home_.path() + "/Moved Game/memcards");
        QCOMPARE(IniSettings(f.configPath()).get("paths.save_dir"), home_.path() + "/elsewhere");
        QCOMPARE(IniSettings(f.configPath()).get("paths.iso"), QStringLiteral("/d/game.iso"));   // the pipeline's other lines kept
        f.choose(home_.path() + "/Example Game");
        f.setSubfolder("saves", home_.path() + "/elsewhere");
        QString err;
        QVERIFY(f.makeSubfolders(&err));
        QVERIFY(QDir(home_.path() + "/Example Game/memcards").exists());
        QVERIFY(QDir(home_.path() + "/elsewhere").exists());
        QVERIFY(!f.prepared().ok);
        PreparedRecord r;
        r.discSha256 = kHelloSha;
        r.targetId = HostPlatform::targetId(manifest_);
        r.binaryPath = home_.path() + "/Example Game/examplegame";
        r.launcherVersion = "0.1";
        QFile bin(r.binaryPath);
        QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write("x");
        bin.close();
        f.recordPrepared(r);
        QVERIFY(f.prepared().ok);
        QVERIFY(f.upToDate(kHelloSha, "0.1"));
        QVERIFY(!f.upToDate(kHelloSha, "0.2"));
        QVERIFY(!f.upToDate("other", "0.1"));
        QVERIFY(!HostPlatform::targetId(manifest_).isEmpty());
        QCOMPARE(GameFolder::executableInside("/x/game"), QStringLiteral("/x/game"));
    }

    void saveStates() {
        AppData data("ExampleGameStates");
        GameFolder f(&data, &manifest_);
        f.choose(home_.path() + "/States");
        SaveStateList list(&manifest_, &f);
        QVERIFY(!list.scan());   // the folder does not exist yet: no entries, not an error
        QDir().mkpath(f.saveStatesFolder());
        QFile s(f.saveStatesFolder() + "/slot0.ps2st");
        QVERIFY(s.open(QIODevice::WriteOnly));
        s.write(QByteArray(4096, 'a'));
        s.close();
        QFile m(f.saveStatesFolder() + "/slot0.ps2st.meta");
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.write("frame=1501\nwall_ms=1792517610563\nbuild_id=x\n");
        m.close();
        QFile tmp(f.saveStatesFolder() + "/slot1.ps2st.tmp");
        QVERIFY(tmp.open(QIODevice::WriteOnly));
        tmp.close();
        QVERIFY(list.scan());
        QCOMPARE(list.entries().size(), 1);
        QCOMPARE(list.entries()[0].name, QStringLiteral("slot0"));
        QCOMPARE(list.entries()[0].frame, quint64(1501));
        QCOMPARE(list.entries()[0].when.toMSecsSinceEpoch(), qint64(1792517610563));
        QVERIFY(list.entries()[0].thumbnailPath.isEmpty());
    }

    void iniRoundTrip() {
        IniSettings ini(home_.path() + "/sub/nativeee.ini");
        QVERIFY(!ini.exists());
        QString err;
        QVERIFY2(ini.set("video.frame_cap", "30", &err), qPrintable(err));
        QVERIFY(ini.exists());
        QCOMPARE(ini.get("video.frame_cap"), QStringLiteral("30"));
        QVERIFY(ini.set("video.vsync", "on", &err));
        QVERIFY(ini.set("video.frame_cap", "60", &err));
        QCOMPARE(ini.get("video.frame_cap"), QStringLiteral("60"));
        QCOMPARE(ini.get("video.vsync"), QStringLiteral("on"));
        QCOMPARE(ini.keys().size(), 2);
        bool changed = false;
        const QString text = IniSettings::setInText("# a comment\nvideo.frame_cap = 30\n", "video.frame_cap", "60", &changed);
        QVERIFY(changed);
        QVERIFY(text.startsWith("# a comment\n"));
        QString v;
        QVERIFY(IniSettings::getFromText(text, "video.frame_cap", &v));
        QCOMPARE(v, QStringLiteral("60"));
    }

    void stepProgress() {
        int done = 0, total = 0;
        QVERIFY(ProcessRunner::parseStepProgress("[12/345] doing a thing", &done, &total));
        QCOMPARE(done, 12);
        QCOMPARE(total, 345);
        QVERIFY(!ProcessRunner::parseStepProgress("something else", &done, &total));
    }

    void libraryTimes() {
        const QDateTime now = QDateTime::fromString("2025-06-10T15:00:00", Qt::ISODate);
        QCOMPARE(GameLibrary::relativeTime(QDateTime(), now), QStringLiteral("Never played"));
        QCOMPARE(GameLibrary::relativeTime(now.addSecs(-30), now), QStringLiteral("Last played just now"));
        QCOMPARE(GameLibrary::relativeTime(now.addSecs(-600), now), QStringLiteral("Last played 10 min ago"));
        QCOMPARE(GameLibrary::relativeTime(now.addSecs(-3 * 3600), now), QStringLiteral("Last played today 12:00"));
        QCOMPARE(GameLibrary::relativeTime(now.addDays(-1), now), QStringLiteral("Last played yesterday 15:00"));
        QCOMPARE(GameLibrary::playtimeText(59), QStringLiteral("under a minute"));
        QCOMPARE(GameLibrary::playtimeText(3660), QStringLiteral("1 h 1 min"));
        QCOMPARE(GeneratedArtwork::initialsOf("Example Game"), QStringLiteral("EG"));
    }

    void buildRule() {
        // the Build button's rule: a chosen, present, supported disc and nothing being prepared
        qputenv("PS2R_LAUNCHER_FAKE_PREPARE", "hang");
        LauncherApp app;
        QString err;
        QVERIFY2(app.loadManifest(home_.path() + "/title.json", &err), qPrintable(err));
        QString why;
        QVERIFY(!app.canPrepare(QString(), &why));
        QVERIFY(why.contains("Choose your disc"));
        QVERIFY(!app.canPrepare(home_.path() + "/never-added.iso", &why));
        const QString good = writeIso(home_, "rule_good.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hello"}});
        const QString pal = writeIso(home_, "rule_pal.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.02", "hello"}});
        const QString junk = home_.path() + "/rule_junk.iso";
        QFile j(junk);
        QVERIFY(j.open(QIODevice::WriteOnly));
        j.write(QByteArray(40 * 1024 * 1024, 'x'));
        j.close();
        // identification of a chosen path, by content: supported / another region / not a disc
        QCOMPARE(app.addDisc(good).status, DiscIdentity::Supported);
        QCOMPARE(app.addDisc(pal).status, DiscIdentity::OtherRegion);
        const DiscIdentity notDisc = app.addDisc(junk);
        QVERIFY(!notDisc.recognised());
        QVERIFY(!app.library()->game(junk));   // an unrecognised file never joins the library
        QVERIFY(app.canPrepare(good, &why));
        QVERIFY(!app.canPrepare(pal, &why));
        QVERIFY(why.contains("European"));
        QVERIFY(!app.canPrepare(junk, &why));
        // no folder yet: the rule is about the disc; the folder is chosen when Build is pressed
        QVERIFY(!app.gameFolder()->chosen());
        app.chooseGameFolder(home_.path() + "/rule_folder", true);
        QCOMPARE(app.gameFolder()->path(), home_.path() + "/rule_folder");   // exact: not made into a "<name>" folder
        QVERIFY(app.prepare(good, QString(), &err));
        QVERIFY(app.preparing());
        QVERIFY(!app.canPrepare(good, &why));   // while preparing
        QVERIFY(why.contains("already"));
        app.preparer()->cancel();
        QVERIFY(!app.preparing());
        QVERIFY(QFile::remove(good));
        app.library()->refreshMissing();
        QVERIFY(!app.canPrepare(good, &why));
        QVERIFY(why.contains("no longer"));
        qunsetenv("PS2R_LAUNCHER_FAKE_PREPARE");
    }

#ifdef PS2R_LAUNCHER_HAS_PREPARE
    void bundledRunner() {
        // the runner over the shipped pipeline: the platform id of a target, this computer only, the index from the
        // manifest or the override, and an honest failure (the pipeline's own sentence) when the index is not there
        BundledPrepareRunner r(&manifest_);
        QCOMPARE(BundledPrepareRunner::platformFor(manifest_, "windows-x64"), QStringLiteral("windows-x86_64"));
        QCOMPARE(BundledPrepareRunner::platformFor(manifest_, "macos-arm64"), QStringLiteral("macos-arm64"));
        QCOMPARE(BundledPrepareRunner::platformFor(manifest_, "no-such"), BundledPrepareRunner::hostPlatform());
        const QString host = HostPlatform::targetId(manifest_);
        QString why;
        QVERIFY(r.available(host, &why));
        QVERIFY(!r.available(host == "windows-x64" ? "linux-x86_64" : "windows-x64", &why));
        QVERIFY(why.contains("only be made on the computer"));
        QCOMPARE(r.indexUrl(), QStringLiteral("https://api.github.com/repos/example/examplegame/releases"));
        r.setIndexUrl(home_.path() + "/no-such-index/releases.json");   // lint: allow
        QCOMPARE(r.estimateText(host), QStringLiteral("about 1 to 2 minutes"));
        PrepareRequest req;
        req.isoPath = writeIso(home_, "bundled.iso", {{"SYSTEM.CNF", "x"}, {"EXMP_000.01", "hello"}});
        req.discSha256 = kHelloSha;
        req.gameFolder = home_.path() + "/bundled_folder";
        req.cacheFolder = req.gameFolder + "/cache";
        req.targetId = host;
        req.binaryName = "examplegame";
        req.logPath = home_.path() + "/bundled.log";
        QSignalSpy failed(&r, &PrepareRunner::failed);
        QSignalSpy finished(&r, &PrepareRunner::finished);
        QString err;
        QVERIFY2(r.start(req, &err), qPrintable(err));
        QVERIFY(r.running());
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 30000);
        QCOMPARE(finished.count(), 0);
        QVERIFY(!r.running());
        const QString text = failed.first().at(0).toString();
        QVERIFY2(!text.isEmpty() && !text.contains("recompil", Qt::CaseInsensitive), qPrintable(text));
        r.waitFinished();
        QVERIFY(!r.lastOk());
        QVERIFY(r.reportJson().contains("\"ok\": false"));
        QVERIFY(r.reportJson().contains("\"driver\": \"launcher\""));
        QVERIFY(QFileInfo::exists(req.logPath));
    }
#endif

    void appDataPaths() {
        AppData data("ExampleGame");
        QVERIFY(data.root().endsWith("/ExampleGame"));
        QVERIFY(data.root().startsWith(home_.path()));
        QCOMPARE(data.expand("~/x"), home_.path() + "/x");
        QCOMPARE(data.expand("${app_data}/Logs"), data.root() + "/Logs");
        QCOMPARE(AppData::shortPath(home_.path() + "/Downloads"), QStringLiteral("~/Downloads"));
        QString err;
        QVERIFY(data.ensureFolders(&err));
        data.setValue("k", 1);
        QVERIFY(data.save(&err));
        AppData again("ExampleGame");
        QCOMPARE(again.value("k").toInt(), 1);
    }
};

QTEST_MAIN(TestLauncherModels)
#include "test_launcher_models.moc"
