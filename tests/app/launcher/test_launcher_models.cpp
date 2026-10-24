// tests/app/launcher/test_launcher_models.cpp -- the launcher's models without a window: the manifest, the ISO 9660
// fingerprint on a synthetic image, the options-file round trip, Ninja's progress line, the pin table, the card's
// relative times.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "app_data.h"
#include "download_pins.h"
#include "game_library.h"
#include "generated_artwork.h"
#include "ini_settings.h"
#include "iso_fingerprint.h"
#include "process_runner.h"
#include "title_manifest.h"
using namespace ps2r::launcher;

namespace {
// A minimal ISO 9660 image: PVD at sector 16, a root directory at sector 18 with the given file names.
QByteArray syntheticIso(const QStringList& files) {
    QByteArray img(2048 * 20, '\0');
    QByteArray pvd(2048, '\0');
    pvd[0] = 1;
    pvd.replace(1, 5, "CD001");
    pvd[6] = 1;
    pvd.replace(40, 32, QByteArray("CRASH6                          "));
    // root directory record at 156: extent 18, length 2048
    QByteArray root(34, '\0');
    root[0] = 34;
    root[2] = 18; root[3] = 0; root[4] = 0; root[5] = 0;
    root[10] = 0; root[11] = 8;   // 2048 little-endian
    root[32] = 1; root[33] = 0;
    pvd.replace(156, 34, root);
    img.replace(16 * 2048, 2048, pvd);
    QByteArray term(2048, '\0');
    term[0] = '\xff';
    term.replace(1, 5, "CD001");
    img.replace(17 * 2048, 2048, term);
    QByteArray dir;
    for (const QString& f : files) {
        const QByteArray id = (f + ";1").toLatin1();
        QByteArray rec(33 + id.size() + (id.size() % 2 == 0 ? 1 : 0), '\0');
        rec[0] = char(rec.size());
        rec[32] = char(id.size());
        rec.replace(33, id.size(), id);
        dir += rec;
    }
    dir.resize(2048);
    img.replace(18 * 2048, 2048, dir);
    // pad to the scanner's minimum
    img.resize(33 * 1024 * 1024);
    return img;
}
QString writeIso(const QTemporaryDir& dir, const QString& name, const QStringList& files) {
    const QString p = dir.path() + "/" + name;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(syntheticIso(files));
    return p;
}
const char* kManifest = R"JSON({
  "id": "SLES-52568", "name": "Crash Twinsanity", "product": "TwinsanityEvolved", "app_data_folder": "TwinsanityEvolved",
  "disc": { "fingerprint": { "file": "SLES_525.68", "region": "PAL", "id": "SLES-52568" }, "alternates": [ { "file": "SLUS_209.09", "region": "NTSC-U", "id": "SLUS-20909" } ] },
  "source": { "url": "/tmp/nowhere" },
  "targets": { "vita": { "label": "PS Vita", "host": "any", "artifact": "vpk", "preset": "vita-release" }, "macos": { "label": "macOS", "host": "macos", "configure": ["-G", "Ninja"], "binary": "twinsanityevolved" }, "windows": { "label": "Windows", "host": "windows", "cross_reason": "needs Windows" } },
  "run": { "iso_flag": "--iso", "args": ["--trace-state"], "title_state_line": "game state @ boundary", "title_state_name": "Title (7)" },
  "settings": { "file": "~/.te-test/ps2r.ini", "groups": [ { "title": "Picture", "rows": [
    { "key": "video.frame_rate", "label": "Frame rate", "kind": "choice", "default": "60", "choices": [ {"value": "30"}, {"value": "60"}, {"value": "uncapped", "label": "Uncapped"} ] },
    { "key": "video.vsync", "label": "Vertical sync", "kind": "switch", "default": "off" } ] } ] },
  "folders": { "games": { "label": "Games", "default": "${app_data}/Games" }, "memory_cards": { "label": "Memory cards", "default": "~/.te-test/memcards" } }
})JSON";
}  // namespace

class TestLauncherModels : public QObject {
    Q_OBJECT
private slots:
    void manifest() {
        TitleManifest m;
        QString err;
        QVERIFY2(m.loadJson(QJsonDocument::fromJson(kManifest).object(), &err), qPrintable(err));
        QCOMPARE(m.id(), QStringLiteral("SLES-52568"));
        QCOMPARE(m.fingerprint().file, QStringLiteral("SLES_525.68"));
        QCOMPARE(m.allFingerprints().size(), 2);
        QCOMPARE(m.targets().size(), 3);
        QCOMPARE(m.targets()[0].id, QStringLiteral("macos"));   // the stable order
        QCOMPARE(m.targets()[2].id, QStringLiteral("vita"));
        QVERIFY(m.target("windows")->crossReason.contains("Windows"));
        QCOMPARE(m.allSettingsRows().size(), 2);
        QCOMPARE(m.allSettingsRows()[0].choices.size(), 3);
        QCOMPARE(m.allSettingsRows()[0].choices[2].label, QStringLiteral("Uncapped"));
        QCOMPARE(m.allSettingsRows()[1].choices.size(), 2);   // a switch gets off/on
        QCOMPARE(m.runArgs(), QStringList{"--trace-state"});
        TitleManifest bad;
        QVERIFY(!bad.loadJson(QJsonObject(), &err));
    }
    void fingerprint() {
        QTemporaryDir dir;
        const QString pal = writeIso(dir, "a.iso", {"SYSTEM.CNF", "SLES_525.68", "CRASH6"});
        const QString other = writeIso(dir, "b.iso", {"SYSTEM.CNF", "SLUS_200.00"});
        QStringList entries;
        QString err;
        QVERIFY2(IsoFingerprint::rootEntries(pal, &entries, &err), qPrintable(err));
        QVERIFY(entries.contains("SLES_525.68"));
        QString matched;
        QCOMPARE(IsoFingerprint::identify(pal, {"SLES_525.68", "SLUS_209.09"}, &matched, &err), IsoFingerprint::Match);
        QCOMPARE(matched, QStringLiteral("SLES_525.68"));
        QCOMPARE(IsoFingerprint::identify(other, {"SLES_525.68", "SLUS_209.09"}, &matched, &err), IsoFingerprint::NotTheGame);
        QCOMPARE(IsoFingerprint::volumeId(pal), QStringLiteral("CRASH6"));
        // not an ISO: random bytes of the right size
        const QString junk = dir.path() + "/junk.iso";
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(40 * 1024 * 1024, 'x'));
        f.close();
        QCOMPARE(IsoFingerprint::identify(junk, {"SLES_525.68"}, &matched, &err), IsoFingerprint::NotAnIso);
        QCOMPARE(IsoFingerprint::identify(dir.path() + "/missing.iso", {"SLES_525.68"}, &matched, &err), IsoFingerprint::Unreadable);
    }
    void iniRoundTrip() {
        QTemporaryDir dir;
        IniSettings ini(dir.path() + "/ps2r.ini");
        QVERIFY(!ini.exists());
        QCOMPARE(ini.get("video.frame_rate", "60"), QStringLiteral("60"));
        QString err;
        QVERIFY2(ini.set("video.frame_rate", "120", &err), qPrintable(err));
        QVERIFY(ini.set("game.language", "english", &err));
        QCOMPARE(ini.get("video.frame_rate"), QStringLiteral("120"));
        QVERIFY(ini.set("video.frame_rate", "uncapped", &err));
        QCOMPARE(ini.get("video.frame_rate"), QStringLiteral("uncapped"));
        QCOMPARE(ini.get("game.language"), QStringLiteral("english"));
        QCOMPARE(ini.keys(), QStringList({"video.frame_rate", "game.language"}));
        // the engine's grammar: a section header and a comment survive, the key inside the section is found by its full name
        const QString text = "[video]\nvsync = on   # the presenter\n";
        QString v;
        QVERIFY(IniSettings::getFromText(text, "video.vsync", &v));
        QCOMPARE(v, QStringLiteral("on"));
        const QString next = IniSettings::setInText(text, "video.vsync", "off");
        QVERIFY(IniSettings::getFromText(next, "video.vsync", &v));
        QCOMPARE(v, QStringLiteral("off"));
        QVERIFY(next.startsWith("[video]\n"));
    }
    void ninjaProgress() {
        int done = 0, total = 0;
        QVERIFY(ProcessRunner::parseNinjaProgress("[12/345] Building CXX object x.o", &done, &total));
        QCOMPARE(done, 12);
        QCOMPARE(total, 345);
        QVERIFY(!ProcessRunner::parseNinjaProgress("-- Configuring done", &done, &total));
        QVERIFY(!ProcessRunner::parseNinjaProgress("[0/0] nothing", &done, &total));
    }
    void pins() {
        QVERIFY(DownloadPins::find("cmake", "macos-arm64"));
        QVERIFY(DownloadPins::find("ninja", "windows-x86_64"));
        QVERIFY(!DownloadPins::find("python", "macos-arm64"));
        for (const DownloadPin& p : DownloadPins::all()) {
            QCOMPARE(p.sha256.size(), 64);
            QVERIFY(p.url.startsWith("https://"));
        }
        QTemporaryDir dir;
        QFile f(dir.path() + "/x");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("abc");
        f.close();
        QCOMPARE(DownloadPins::sha256Of(dir.path() + "/x"), QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }
    void libraryTimes() {
        const QDateTime now(QDate(2026, 10, 19), QTime(12, 0));
        QCOMPARE(GameLibrary::relativeTime(QDateTime(), now), QStringLiteral("Never played"));
        QCOMPARE(GameLibrary::relativeTime(now.addSecs(-30), now), QStringLiteral("Last played just now"));
        QCOMPARE(GameLibrary::relativeTime(now.addSecs(-600), now), QStringLiteral("Last played 10 min ago"));
        QCOMPARE(GameLibrary::relativeTime(now.addDays(-1), now), QStringLiteral("Last played yesterday 12:00"));
        QCOMPARE(GameLibrary::playtimeText(59), QStringLiteral("under a minute"));
        QCOMPARE(GameLibrary::playtimeText(22320), QStringLiteral("6 h 12 min"));
        QCOMPARE(GeneratedArtwork::initialsOf("Crash Twinsanity"), QStringLiteral("CT"));
    }
    void appDataPaths() {
        qputenv("HOME", "/tmp/te-home");
        qputenv("USERPROFILE", "/tmp/te-home");   // AppData::homePath() reads USERPROFILE on Windows
        AppData a("TwinsanityEvolved");
#ifdef Q_OS_MACOS
        QCOMPARE(a.root(), QStringLiteral("/tmp/te-home/Library/Application Support/TwinsanityEvolved"));
#endif
        QCOMPARE(a.expand("~/.te/x"), QStringLiteral("/tmp/te-home/.te/x"));
        QCOMPARE(a.expand("${app_data}/Games"), a.root() + "/Games");
        QCOMPARE(AppData::shortPath("/tmp/te-home/Downloads"), QStringLiteral("~/Downloads"));
    }
};

QTEST_MAIN(TestLauncherModels)
#include "test_launcher_models.moc"
