// app/launcher/launcher_app.cpp -- the model wired together.
#include "launcher_app.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include "app_data.h"
#include "disc_identifier.h"
#include "game_folder.h"
#include "game_library.h"
#include "game_process.h"
#include "host_platform.h"
#include "ini_settings.h"
#include "prepare_runner.h"
#include "shipped_game_runner.h"
#include "title_manifest.h"
namespace ps2r::launcher {

LauncherApp::LauncherApp(QObject* parent) : QObject(parent) {}

LauncherApp::~LauncherApp() {
    delete settings_;
    delete folder_;
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
        else if (fi.isDir()) for (const QString& f : QDir(p).entryList({"*.json"}, QDir::Files, QDir::Name)) candidates << p + "/" + f;   // lint: allow
    }
    QString firstError;
    for (const QString& c : candidates) {
        TitleManifest* m = new TitleManifest();
        QString err;
        if (m->load(c, &err)) { manifest_ = m; break; }
        if (firstError.isEmpty()) firstError = err;
        delete m;
    }
    if (!manifest_) {
        if (error) *error = candidates.isEmpty() ? QStringLiteral("No game description was found beside the application.") : firstError;
        return false;
    }
    appData_ = new AppData(manifest_->appDataFolder());
    QString err;
    appData_->ensureFolders(&err);
    library_ = new GameLibrary(appData_, this);
    folder_ = new GameFolder(appData_, manifest_);
    preparer_ = PrepareRunner::createDefault(manifest_, this);
    game_ = new GameProcess(manifest_, this);
    // tests and scripted runs name the game folder up front instead of answering the question
    const QString preset = qEnvironmentVariable("PS2R_LAUNCHER_GAME_FOLDER");
    if (!preset.isEmpty() && !folder_->chosen()) folder_->choose(preset);
    openSettings();
    connect(preparer_, &PrepareRunner::finished, this, [this](const QString& binary) {
        // the pipeline wrote a fresh options file (D-L13): the player's folder choices go back in first, so the game
        // started from the folder reads the same memory cards and save states the launcher shows
        folder_->setSubfolderChoices(subfoldersAtBuild_);
        PreparedRecord r;
        r.ok = true;
        r.discSha256 = preparingSha_;
        r.discPath = preparingIso_;
        r.targetId = preparingTarget_;
        r.binaryPath = binary;
        r.launcherVersion = version();
        folder_->recordPrepared(r);
        preparingIso_.clear();
        emit prepared(binary);
        emit stateChanged();
    });
    connect(preparer_, &PrepareRunner::failed, this, [this](const QString&, const QString&) { preparingIso_.clear(); emit stateChanged(); });
    connect(game_, &GameProcess::finished, this, [this](bool, qint64 seconds) {
        if (!playingIso_.isEmpty()) library_->recordPlayEnd(playingIso_, seconds);
        playingIso_.clear();
        emit stateChanged();
    });
    connect(library_, &GameLibrary::changed, this, &LauncherApp::stateChanged);
    return true;
}

void LauncherApp::openSettings() {
    delete settings_;
    settings_ = folder_->chosen() ? new IniSettings(settingsFilePath()) : nullptr;
}

void LauncherApp::chooseGameFolder(const QString& pickedOrFolder, bool exact) {
    folder_->choose(exact ? pickedOrFolder : folder_->folderFor(pickedOrFolder));
    openSettings();
    emit foldersChanged();
    emit stateChanged();
}

void LauncherApp::setSubfolder(const QString& id, const QString& path) {
    folder_->setSubfolder(id, path);
    const QString key = folder_->subfolderKey(id);
    if (!key.isEmpty() && folder_->chosen()) emit settingsChanged(key);
    emit foldersChanged();
}

QString LauncherApp::settingsFilePath() const { return folder_->chosen() ? folder_->configPath() : QString(); }

QString LauncherApp::settingValue(const QString& key) const {
    const QString v = settings_ ? settings_->get(key) : QString();
    if (!v.isEmpty()) return v;
    for (const SettingsRowSpec& r : manifest_->allSettingsRows()) if (r.key == key) return r.defaultValue;
    return QString();
}

bool LauncherApp::setSettingValue(const QString& key, const QString& value, QString* error) {
    if (!folder_->chosen() || !settings_) { if (error) *error = QStringLiteral("Choose where the game lives first."); return false; }
    if (!settings_->set(key, value, error)) return false;
    emit settingsChanged(key);
    return true;
}

DiscIdentity LauncherApp::addDisc(const QString& path) {
    const DiscIdentity d = DiscIdentifier::identify(*manifest_, path);
    if (d.recognised()) library_->add(d);
    return d;
}

bool LauncherApp::canPrepare(const QString& isoPath, QString* why) const {
    if (isoPath.isEmpty()) { if (why) *why = QStringLiteral("Choose your disc image first."); return false; }
    const GameEntry* g = library_->game(isoPath);
    if (!g) { if (why) *why = QStringLiteral("Choose your disc image first."); return false; }
    if (g->missing) { if (why) *why = QStringLiteral("The disc image is no longer where it was."); return false; }
    if (!g->identity.playable()) { if (why) *why = g->identity.reason; return false; }
    if (preparer_->running()) { if (why) *why = QStringLiteral("The game is already being prepared."); return false; }
    return true;
}

QString LauncherApp::currentDisc() const {
    const QString chosen = appData_->value("current_disc").toString();
    if (!chosen.isEmpty()) {
        const GameEntry* g = library_->game(chosen);
        if (g && !g->missing && g->identity.playable()) return chosen;
    }
    for (const GameEntry& g : library_->games()) if (!g.missing && g.identity.playable()) return g.path;
    return QString();
}

void LauncherApp::setCurrentDisc(const QString& isoPath) {
    appData_->setValue("current_disc", isoPath);
    appData_->save();
    emit stateChanged();
}

QString LauncherApp::hostTarget() const { return HostPlatform::targetId(*manifest_); }

LauncherApp::Action LauncherApp::nextAction(const QString& isoPath) const {
    const GameEntry* g = library_->game(isoPath);
    if (!g || g->missing) return Missing;
    if (!g->identity.playable()) return Unsupported;
    if (game_->running() && game_->isoPath() == isoPath) return Playing;
    if (!folder_->chosen()) return NoFolder;
    if (!folder_->upToDate(g->identity.sha256, version())) return Prepare;
    return Play;
}

bool LauncherApp::preparing() const { return preparer_->running(); }

bool LauncherApp::prepare(const QString& isoPath, const QString& targetId, QString* error) {
    const GameEntry* g = library_->game(isoPath);
    if (!g || !g->identity.playable()) { if (error) *error = QStringLiteral("This disc image cannot be prepared."); return false; }
    if (!folder_->chosen()) { if (error) *error = QStringLiteral("Choose where the game should live first."); return false; }
    if (preparer_->running()) { if (error) *error = QStringLiteral("The game is already being prepared."); return false; }
    QString err;
    subfoldersAtBuild_ = folder_->subfolderChoices();   // kept for after the pipeline's rewrite (D-L13)
    if (!folder_->writeSubfolderKeys(&err)) { if (error) *error = err; return false; }
    if (!folder_->makeSubfolders(&err)) { if (error) *error = err; return false; }
    const QString target = targetId.isEmpty() ? hostTarget() : targetId;
    const TargetSpec* t = manifest_->target(target);
    PrepareRequest req;
    req.isoPath = isoPath;
    req.discSha256 = g->identity.sha256;
    req.gameFolder = folder_->path();
    req.cacheFolder = folder_->subfolder("cache");
    req.targetId = target;
    req.binaryName = t ? t->binary : manifest_->binaryName();
    req.logPath = appData_->logPath("prepare");
    req.appDataFolder = appData_->root();
    if (!preparer_->start(req, error)) return false;
    preparingIso_ = isoPath;
    preparingTarget_ = target;
    preparingSha_ = g->identity.sha256;
    setCurrentDisc(isoPath);
    emit stateChanged();
    return true;
}

bool LauncherApp::play(const QString& isoPath, QString* error, const QString& statePath) {
    if (game_->running()) { if (error) *error = QStringLiteral("The game is already running."); return false; }
    const PreparedRecord r = folder_->prepared();
    if (!r.ok) { if (error) *error = QStringLiteral("The game has not been prepared yet."); return false; }
    const QString exe = GameFolder::executableInside(r.binaryPath);
    QString err;
    folder_->makeSubfolders(&err);
    if (!game_->start(exe, isoPath, folder_->runArgs(isoPath, statePath), appData_->logPath("play"), error)) return false;
    setCurrentDisc(isoPath);
    playingIso_ = isoPath;
    library_->recordPlayStart(isoPath);
    emit stateChanged();
    return true;
}

void LauncherApp::stopGame() { game_->stop(); }

bool LauncherApp::setupComplete() const {
    if (!folder_->chosen()) return false;
    const QString disc = currentDisc();
    if (disc.isEmpty()) return false;
    const GameEntry* g = library_->game(disc);
    return g && folder_->upToDate(g->identity.sha256, version());
}

bool LauncherApp::refreshGame() {
    if (!qobject_cast<ShippedGameRunner*>(preparer_) || preparer_->running() || game_->running()) return false;
    const PreparedRecord r = folder_->prepared();
    if (!r.ok || !folder_->chosen() || folder_->upToDate(r.discSha256, version())) return false;
    const GameEntry* g = library_->game(r.discPath);
    if (!g || g->missing || !g->identity.playable() || g->identity.sha256 != r.discSha256) return false;
    QString err;
    return prepare(r.discPath, r.targetId, &err);
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
