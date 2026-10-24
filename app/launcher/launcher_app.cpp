// app/launcher/launcher_app.cpp -- the model wired together.
#include "launcher_app.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include "app_data.h"
#include "build_runner.h"
#include "game_library.h"
#include "game_process.h"
#include "ini_settings.h"
#include "installer.h"
#include "iso_fingerprint.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
bool copyFolder(const QString& from, const QString& to) {
    QDir src(from);
    if (!src.exists()) return false;
    QDir().mkpath(to);
    for (const QFileInfo& fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dest = to + "/" + fi.fileName();
        if (fi.isDir()) copyFolder(fi.absoluteFilePath(), dest);
        else if (!QFileInfo::exists(dest)) QFile::copy(fi.absoluteFilePath(), dest);
    }
    return true;
}
}  // namespace

LauncherApp::LauncherApp(QObject* parent) : QObject(parent) {
#ifdef Q_OS_WIN
    // Every git the launcher starts (the source, vcpkg, and vcpkg's own during the build) verifies HTTPS against the
    // Windows certificate store: Git for Windows' bundled OpenSSL list rejects the certificates of antivirus HTTPS
    // scanning and company proxies. Set through git's environment config unless the user already set some.
    if (!qEnvironmentVariableIsSet("GIT_CONFIG_COUNT")) {
        qputenv("GIT_CONFIG_COUNT", "1");
        qputenv("GIT_CONFIG_KEY_0", "http.sslBackend");
        qputenv("GIT_CONFIG_VALUE_0", "schannel");
    }
#endif
}

LauncherApp::~LauncherApp() {
    delete settings_;
    delete manifest_;
    delete appData_;
}

QStringList LauncherApp::manifestSearchPaths() {
    QStringList dirs;
    const QString env = QProcessEnvironment::systemEnvironment().value("PS2R_LAUNCHER_MANIFEST");
    if (!env.isEmpty()) dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + "/../Resources/titles" << app + "/titles" << app + "/../titles" << app + "/../../titles";
    return dirs;
}

bool LauncherApp::loadManifest(const QString& explicitPath, QString* error) {
    QStringList candidates;
    if (!explicitPath.isEmpty()) candidates << explicitPath;
    for (const QString& p : manifestSearchPaths()) {
        const QFileInfo fi(p);
        if (fi.isFile()) candidates << p;
        else if (fi.isDir()) for (const QString& f : QDir(p).entryList({"*.json"}, QDir::Files, QDir::Name)) candidates << p + "/" + f;
    }
    QString firstError;
    for (const QString& c : candidates) {
        TitleManifest* m = new TitleManifest();
        QString err;
        if (m->load(c, &err)) {
            manifest_ = m;
            break;
        }
        if (firstError.isEmpty()) firstError = err;
        delete m;
    }
    if (!manifest_) {
        if (error) *error = candidates.isEmpty() ? QStringLiteral("No title manifest was found beside the application (titles/*.json).") : firstError;
        return false;
    }
    appData_ = new AppData(manifest_->appDataFolder());
    QString err;
    appData_->ensureFolders(&err);
    library_ = new GameLibrary(appData_, this);
    buildFromSource_ = qEnvironmentVariable("PS2R_LAUNCHER_BUILD_FROM_SOURCE") == "1" || appData_->value("build_from_source").toBool();
    installer_ = new Installer(appData_, manifest_, buildFromSource_, this);
    builder_ = new BuildRunner(appData_, manifest_, installer_, this);
    game_ = new GameProcess(manifest_, this);
    scanner_ = new DiscScanner(this);
    settings_ = new IniSettings(settingsFilePath());
    seedFolders();
    connect(scanner_, &DiscScanner::found, this, [this](const DiscFound& d) { library_->add(d.path, d.fingerprintFile, d.sizeBytes); });
    connect(installer_, &Installer::stepChanged, this, [this](int) { emit stateChanged(); });
    connect(installer_, &Installer::finished, this, [this](bool ok) {
        // download mode: the downloaded game gets its disc and shortcuts
        if (ok && !buildFromSource_ && !installer_->installedGameExe().isEmpty())
            finishInstall(QFileInfo(installer_->installedGameExe()).absolutePath(), installer_->installedGameExe());
        emit stateChanged();
    });
    connect(builder_, &BuildRunner::finished, this, [this](const QString& target, bool ok, const BuildRecord&) {
        // a build for this machine is installed to the game folder at once
        if (ok && target == hostTarget()) exportGame();
        emit stateChanged();
    });
    connect(game_, &GameProcess::finished, this, [this](bool, qint64 seconds) {
        if (!playingIso_.isEmpty()) library_->recordPlayEnd(playingIso_, seconds);
        playingIso_.clear();
        emit stateChanged();
    });
    connect(library_, &GameLibrary::changed, this, &LauncherApp::stateChanged);
    // tests and scripted installs name the game folder up front instead of answering the question
    const QString preset = qEnvironmentVariable("PS2R_LAUNCHER_GAME_FOLDER");
    if (!preset.isEmpty() && !gameFolderChosen()) {
        appData_->setValue("folder.game_output", QDir::cleanPath(preset));
        appData_->save();
    }
    if (gameFolderChosen()) installer_->setGameFolder(folder("game_output"));
    // a build already made (an earlier launcher, or the folder moved): bring the installed copy up to date
    const BuildRecord r = builder_->record(hostTarget());
    if (buildFromSource_ && r.ok && gameFolderChosen() && appData_->value("installed_game_commit").toString() != r.sourceCommit) exportGame();
    return true;
}

void LauncherApp::seedFolders() {
    // the game folder is made when the game is installed into it, not before the user chose it
    for (const FolderSpec& f : manifest_->folders()) if (f.id != "game_output") QDir().mkpath(folder(f.id));
}

QString LauncherApp::folder(const QString& id) const {
    const QString chosen = appData_->value("folder." + id).toString();
    if (!chosen.isEmpty()) return chosen;
    for (const FolderSpec& f : manifest_->folders()) {
        if (f.id == id) return appData_->expand(f.defaultPath, appData_->value("folder.games").toString());
    }
    if (id == "games") return appData_->defaultGamesDir();
    if (id == "mods") return appData_->defaultModsDir();
    return QString();
}

void LauncherApp::setFolder(const QString& id, const QString& path) {
    appData_->setValue("folder." + id, path);
    appData_->save();
    QDir().mkpath(path);
    emit foldersChanged();
}

QString LauncherApp::settingsFilePath() const {
    const QString f = manifest_->settingsFile();
    return appData_->expand(f.isEmpty() ? QStringLiteral("${app_data}/ps2r.ini") : f);
}

QString LauncherApp::settingValue(const QString& key) const {
    QString v = settings_->get(key);
    if (!v.isEmpty()) return v;
    for (const SettingsRowSpec& r : manifest_->allSettingsRows()) if (r.key == key) return r.defaultValue;
    return QString();
}

bool LauncherApp::setSettingValue(const QString& key, const QString& value, QString* error) {
    if (!settings_->set(key, value, error)) return false;
    emit settingsChanged(key);
    return true;
}

void LauncherApp::seedModsFolder() {
    for (const FolderSpec& f : manifest_->folders()) {
        if (f.id != "mods" || f.seedFromSource.isEmpty()) continue;
        const QString src = installer_->sourceDir() + "/" + f.seedFromSource;
        if (QDir(src).exists()) copyFolder(src, folder("mods"));
    }
}

void LauncherApp::scan() {
    QStringList folders;
    for (const QString& f : manifest_->scanFolders()) folders << appData_->expand(f, folder("games"));
    folders << folder("games");
    folders.removeDuplicates();
    QStringList files;
    for (const DiscFingerprint& fp : manifest_->allFingerprints()) files << fp.file;
    scanner_->start(folders, manifest_->scanRemovable(), manifest_->scanMaxDepth(), manifest_->discExtensions(), files);
}

bool LauncherApp::addDisc(const QString& path, QString* reason) {
    QStringList files;
    for (const DiscFingerprint& fp : manifest_->allFingerprints()) files << fp.file;
    QString matched, err;
    switch (IsoFingerprint::identify(path, files, &matched, &err)) {
    case IsoFingerprint::Match:
        library_->add(path, matched, QFileInfo(path).size());
        return true;
    case IsoFingerprint::NotTheGame:
        if (reason) *reason = QStringLiteral("This is not a %1 disc image. \"%2\" has no %3 on it. Choose a disc image of the game you own.")
                                  .arg(manifest_->name(), path, files.join(" or "));
        return false;
    case IsoFingerprint::NotAnIso:
        if (reason) *reason = QStringLiteral("\"%1\" cannot be read as a disc image (%2). It may be a damaged download or a different format "
                                             "(a .bin with a .cue is not supported yet).").arg(path, err);
        return false;
    case IsoFingerprint::Unreadable:
    default:
        if (reason) *reason = QStringLiteral("\"%1\" could not be opened (%2).").arg(path, err);
        return false;
    }
}

QString LauncherApp::cardSubtitle(const TitleManifest& m, const QString& fingerprintFile, const QString& path) {
    QString region, id;
    for (const DiscFingerprint& fp : m.allFingerprints()) {
        if (fp.file.compare(fingerprintFile, Qt::CaseInsensitive) == 0) { region = fp.region; id = fp.id; break; }
    }
    QStringList parts;
    if (!region.isEmpty()) parts << region;
    if (!id.isEmpty()) parts << id;
    parts << QFileInfo(path).fileName();
    return parts.join(QStringLiteral(" · "));
}

QString LauncherApp::hostTarget() const { return BuildRunner::hostTargetId(); }

LauncherApp::Action LauncherApp::nextAction(const QString& isoPath) const {
    const GameEntry* g = library_->game(isoPath);
    if (!g || g->missing) return Missing;
    if (game_->running() && game_->isoPath() == isoPath) return Playing;
    if (!installer_->complete()) return Install;
    if (!buildFromSource_) return Play;   // the downloaded game is the one that plays
    if (!builder_->upToDate(hostTarget())) return Build;
    return Play;
}

void LauncherApp::install() { installer_->start(); }

void LauncherApp::build(const QString& targetId) {
    seedModsFolder();
    builder_->start(targetId);
}

bool LauncherApp::play(const QString& isoPath, QString* error) {
    if (game_->running()) { if (error) *error = QStringLiteral("a game is already running"); return false; }
    QString binary;
    if (buildFromSource_) {
        const BuildRecord r = builder_->record(hostTarget());
        if (!r.ok) { if (error) *error = QStringLiteral("no build for this machine yet"); return false; }
        binary = r.binaryPath;
    } else {
        binary = installer_->installedGameExe();
        if (binary.isEmpty()) { if (error) *error = QStringLiteral("the game is not installed yet"); return false; }
        writeDiscImage(QFileInfo(binary).absolutePath(), isoPath);   // the shortcut starts with the disc played last
    }
    seedModsFolder();
    QStringList extra;
    const QString mc = folder("memory_cards");
    if (!game_->start(binary, isoPath, settingsFilePath(), mc, extra, appData_->logPath("play"), error)) return false;
    appData_->setValue("last_disc", isoPath);   // the disc the installed game names
    appData_->save();
    playingIso_ = isoPath;
    library_->recordPlayStart(isoPath);
    emit stateChanged();
    return true;
}

void LauncherApp::stopGame() { game_->stop(); }

bool LauncherApp::gameFolderChosen() const { return !appData_->value("folder.game_output").toString().isEmpty(); }

void LauncherApp::chooseGameFolder(const QString& parentOrFolder) {
    // a folder of the title's name inside the one picked, unless the pick already is it
    QString dir = QDir::cleanPath(parentOrFolder);
    if (QFileInfo(dir).fileName().compare(manifest_->name(), Qt::CaseInsensitive) != 0) dir += "/" + manifest_->name();
    appData_->setValue("folder.game_output", dir);
    appData_->save();
    installer_->setGameFolder(dir);
    emit foldersChanged();
    if (buildFromSource_ && builder_->record(hostTarget()).ok) exportGame();
}

QString LauncherApp::discForExport() const {
    // the disc last played, else the first one the library has
    const QString last = appData_->value("last_disc").toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) return last;
    for (const GameEntry& g : library_->games()) if (!g.missing && QFileInfo::exists(g.path)) return g.path;
    return QString();
}

namespace {
bool copyTreeOver(const QString& from, const QString& to, QString* failed) {
    QDir().mkpath(to);
    for (const QFileInfo& fi : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dest = to + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyTreeOver(fi.absoluteFilePath(), dest, failed)) return false;
            continue;
        }
        QFile::remove(dest);
        if (!QFile::copy(fi.absoluteFilePath(), dest)) { *failed = dest; return false; }
    }
    return true;
}

#ifdef Q_OS_WIN
void makeShortcut(const QString& lnk, const QString& target, const QString& workDir, const QString& description) {
    QDir().mkpath(QFileInfo(lnk).absolutePath());
    auto quote = [](QString s) { return "'" + s.replace("'", "''") + "'"; };
    const QString script = QStringLiteral("$s = (New-Object -ComObject WScript.Shell).CreateShortcut(%1); $s.TargetPath = %2; "
                                          "$s.WorkingDirectory = %3; $s.Description = %4; $s.Save()")
                               .arg(quote(QDir::toNativeSeparators(lnk)), quote(QDir::toNativeSeparators(target)),
                                    quote(QDir::toNativeSeparators(workDir)), quote(description));
    QProcess::execute("powershell", {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script});
}
#endif
}  // namespace

bool LauncherApp::exportGame(QString* error) {
    auto fail = [&](const QString& why) {
        exportError_ = why;
        if (error) *error = why;
        emit stateChanged();
        return false;
    };
    const BuildRecord r = builder_->record(hostTarget());
    if (!r.ok || !QFileInfo::exists(r.binaryPath)) return fail(QStringLiteral("the game has not been built yet"));
    if (!gameFolderChosen()) return fail(QStringLiteral("no game folder has been chosen"));
    const QString out = folder("game_output");
    if (!QDir().mkpath(out)) return fail(QStringLiteral("the game folder %1 could not be made").arg(QDir::toNativeSeparators(out)));
    // what goes in: the release folder the build staged (Windows: the executable, its DLLs, the notices), else the binary
    const QString exeName = QFileInfo(r.binaryPath).fileName();
    const QString staged = installer_->sourceDir() + "/dist/windows-x64";
    QString failed;
#ifdef Q_OS_WIN
    const bool useStaged = QFileInfo::exists(staged + "/" + exeName);
#else
    const bool useStaged = false;
#endif
    if (useStaged) {
        if (!copyTreeOver(staged, out, &failed))
            return fail(QStringLiteral("%1 could not be written (is the game still running?)").arg(QDir::toNativeSeparators(failed)));
    } else {
        const QString dest = out + "/" + exeName;
        QFile::remove(dest);
        if (!QFile::copy(r.binaryPath, dest))
            return fail(QStringLiteral("%1 could not be written (is the game still running?)").arg(QDir::toNativeSeparators(dest)));
        QFile(dest).setPermissions(QFile(dest).permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        for (const QString& f : {QStringLiteral("LICENSE"), QStringLiteral("README.md"), QStringLiteral("THIRD_PARTY_NOTICES.md")}) {
            QFile::remove(out + "/" + f);
            QFile::copy(installer_->sourceDir() + "/" + f, out + "/" + f);
        }
    }
    appData_->setValue("installed_game_commit", r.sourceCommit);
    finishInstall(out, out + "/" + exeName);
    return true;
}

void LauncherApp::writeDiscImage(const QString& folder, const QString& disc) const {
    // the disc image the game starts with when it is given none (runner/te_startup.h); none known: the game asks itself
    if (disc.isEmpty()) return;
    QFile f(folder + "/disc-image.txt");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
#ifdef Q_OS_WIN
    f.write(QDir::toNativeSeparators(disc).toLocal8Bit());   // the C runtime opens it in the system code page
#else
    f.write(disc.toUtf8());
#endif
    f.write("\n");
}

void LauncherApp::finishInstall(const QString& folder, const QString& exe) {
    writeDiscImage(folder, discForExport());
#ifdef Q_OS_WIN
    const QString name = manifest_->name();
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString startMenu = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!desktop.isEmpty()) makeShortcut(desktop + "/" + name + ".lnk", exe, folder, name);
    if (!startMenu.isEmpty()) makeShortcut(startMenu + "/" + name + ".lnk", exe, folder, name);
#endif
    appData_->setValue("installed_game", exe);
    appData_->save();
    exportError_.clear();
    emit gameExported(folder);
    emit stateChanged();
}

void LauncherApp::setBuildFromSource(bool on) {
    appData_->setValue("build_from_source", on);
    appData_->save();
}

QString LauncherApp::installedGame() const {
    const QString exe = appData_->value("installed_game").toString();
    return QFileInfo::exists(exe) ? exe : QString();
}

QString LauncherApp::version() const {
#ifdef PS2R_LAUNCHER_VERSION
    return QStringLiteral(PS2R_LAUNCHER_VERSION);
#else
    return QStringLiteral("0.1");
#endif
}

QString LauncherApp::engineCommit() const {
    // the engine commit the launcher was built from, stamped by the packaging into Resources/BUILD-ID when present
    for (const QString& p : {QCoreApplication::applicationDirPath() + "/../Resources/BUILD-ID", QCoreApplication::applicationDirPath() + "/BUILD-ID"}) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            for (const QString& l : QString::fromUtf8(f.readAll()).split('\n')) if (l.startsWith("engine")) return l.section(' ', 1).simplified().left(12);
        }
    }
    return QString();
}

}  // namespace ps2r::launcher
