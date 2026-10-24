// app/launcher/game_folder.cpp -- the game folder.
#include "game_folder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "app_data.h"
#include "host_platform.h"
#include "ini_settings.h"
#include "title_manifest.h"
namespace ps2r::launcher {

GameFolder::GameFolder(AppData* appData, const TitleManifest* manifest) : appData_(appData), manifest_(manifest) {}

bool GameFolder::chosen() const { return !path().isEmpty(); }
QString GameFolder::path() const { return appData_->value("game_folder").toString(); }

void GameFolder::choose(const QString& folder) {
    // the choices made so far travel with the player (D-L15): a relative one follows into the new folder, an absolute
    // one stays where it is; both land in the new folder's options file
    const QMap<QString, QString> choices = subfolderChoices();
    appData_->setValue("game_folder", QDir::cleanPath(folder));
    appData_->save();
    for (auto it = choices.begin(); it != choices.end(); ++it) pending_[it.key()] = it.value();
    writeSubfolderKeys();
}

QString GameFolder::suggestedPath() const {
    // decision D-L10/D-L12: the game lives in Documents by default (the manifest may say otherwise); under the home
    // AppData uses (the HOME variable), so a test or a portable run with another HOME never writes into the real one
    const QString fromManifest = manifest_->defaultGameFolder();
    if (!fromManifest.isEmpty()) return QDir::cleanPath(appData_->expand(fromManifest));
    return AppData::homePath() + "/Documents/" + safeFolderName(manifest_->name());
}

QString GameFolder::defaultParent() const {
    const QString parent = QFileInfo(suggestedPath()).absolutePath();
    return QDir(parent).exists() ? parent : AppData::homePath();
}

QString GameFolder::safeFolderName(const QString& name) {
    // a folder name every file system takes: no : / \ * ? " < > |, no run of spaces, no trailing dot
    QString s = name;
    static const QString bad = QStringLiteral(":/\\*?\"<>|");
    for (QChar& ch : s) if (bad.contains(ch)) ch = ' ';
    s = s.simplified();
    while (s.endsWith('.')) s.chop(1);
    return s.isEmpty() ? QStringLiteral("Game") : s;
}

QString GameFolder::folderFor(const QString& picked) const {
    const QString dir = QDir::cleanPath(picked);
    const QString leaf = QFileInfo(dir).fileName();
    if (leaf.compare(safeFolderName(manifest_->name()), Qt::CaseInsensitive) == 0) return dir;
    if (leaf.compare(manifest_->product(), Qt::CaseInsensitive) == 0) return dir;
    if (QFileInfo::exists(dir + "/" + manifest_->configFileName()) || QFileInfo::exists(dir + "/prepared.json")) return dir;   // an existing game folder   // lint: allow
    return dir + "/" + safeFolderName(manifest_->name());
}

QString GameFolder::configPath() const { return path() + "/" + manifest_->configFileName(); }

QString GameFolder::binaryPath(const QString& targetId) const {
    const TargetSpec* t = manifest_->target(targetId.isEmpty() ? HostPlatform::targetId(*manifest_) : targetId);
    const QString name = t ? t->binary : manifest_->binaryName();
    return path() + "/" + name;
}

QString GameFolder::executableInside(const QString& binaryPath) {
    if (binaryPath.endsWith(QLatin1String(".app"))) {
        const QString base = QFileInfo(binaryPath).completeBaseName();
        const QString inside = binaryPath + "/Contents/MacOS/" + base;
        if (QFileInfo::exists(inside)) return inside;
        const QFileInfoList all = QDir(binaryPath + "/Contents/MacOS").entryInfoList(QDir::Files | QDir::Executable);
        if (!all.isEmpty()) return all.first().absoluteFilePath();
    }
    return binaryPath;
}

QStringList GameFolder::subfolderIds() const {
    QStringList ids;
    for (const SubfolderSpec& s : manifest_->subfolders()) ids << s.id;
    return ids;
}

QString GameFolder::subfolderKey(const QString& id) const {
    const SubfolderSpec* s = manifest_->subfolder(id);
    return s ? s->key : QString();
}

QString GameFolder::relativeIfInside(const QString& path, const QString& folder) {
    const QString p = QDir::cleanPath(path), f = QDir::cleanPath(folder);
    if (f.isEmpty() || p.isEmpty()) return p;
    if (p == f) return QStringLiteral(".");
    if (p.startsWith(f + "/")) return p.mid(f.size() + 1);
    return p;
}

QString GameFolder::resolveIn(const QString& choice, const QString& folder) {
    if (QDir::isAbsolutePath(choice)) return QDir::cleanPath(choice);
    return QDir::cleanPath(folder + "/" + choice);
}

QString GameFolder::storedSubfolder(const QString& id) const {
    const QString key = subfolderKey(id);
    if (key.isEmpty() || !chosen()) return QString();
    return IniSettings(configPath()).get(key);
}

QString GameFolder::subfolderChoice(const QString& id) const {
    // the choice: what waits for the first write, else what the options file says (made relative when it is inside
    // the game folder), else the manifest's default name inside the game folder
    if (pending_.contains(id)) return pending_.value(id);
    const QString stored = storedSubfolder(id);
    if (!stored.isEmpty()) return relativeIfInside(stored, path());
    const SubfolderSpec* s = manifest_->subfolder(id);
    return s ? s->defaultName : id;
}

bool GameFolder::subfolderInside(const QString& id) const { return !QDir::isAbsolutePath(subfolderChoice(id)); }

QString GameFolder::subfolderIn(const QString& id, const QString& folder) const { return resolveIn(subfolderChoice(id), folder); }

QString GameFolder::subfolder(const QString& id) const { return subfolderIn(id, path()); }

void GameFolder::setSubfolder(const QString& id, const QString& folder) {
    if (subfolderKey(id).isEmpty()) return;   // fixed at <folder>/<default>: nothing to choose
    // decision D-L15: inside the game folder (or the suggested one, before any is chosen) -> relative, follows the game
    // folder; anywhere else -> absolute, stays where it is
    const QString base = chosen() ? path() : suggestedPath();
    pending_[id] = relativeIfInside(folder, base);
    if (chosen()) writeSubfolderKeys();
}

QMap<QString, QString> GameFolder::subfolderChoices() const {
    QMap<QString, QString> out;
    for (const QString& id : subfolderIds()) if (!subfolderKey(id).isEmpty()) out[id] = subfolderChoice(id);
    return out;
}

void GameFolder::setSubfolderChoices(const QMap<QString, QString>& choices) {
    for (auto it = choices.begin(); it != choices.end(); ++it) if (!subfolderKey(it.key()).isEmpty()) pending_[it.key()] = it.value();
    if (chosen()) writeSubfolderKeys();
}

bool GameFolder::writeSubfolderKeys(QString* error) {
    if (!chosen()) { if (error) *error = QStringLiteral("Choose where the game lives first."); return false; }
    IniSettings ini(configPath());
    bool ok = true;
    for (const QString& id : subfolderIds()) {
        const QString key = subfolderKey(id);
        if (key.isEmpty()) continue;
        const QString absolute = subfolder(id);   // the game reads an absolute path; the relative form is the launcher's
        QString err;
        if (ini.set(key, absolute, &err)) pending_.remove(id);
        else { ok = false; if (error) *error = err; }
    }
    return ok;
}

bool GameFolder::makeSubfolders(QString* error) const {
    for (const QString& d : QStringList{path()} + [this] { QStringList l; for (const QString& id : subfolderIds()) l << subfolder(id); return l; }()) {
        if (!QDir().mkpath(d)) {
            if (error) *error = QStringLiteral("The folder %1 could not be made.").arg(QDir::toNativeSeparators(d));
            return false;
        }
    }
    return true;
}

QString GameFolder::expand(const QString& text, const QString& isoPath, const QString& statePath) const {
    QString s = text;
    s.replace("${iso}", isoPath);
    s.replace("${state}", statePath);
    s.replace("${config}", configPath());
    s.replace("${folder}", path());
    for (const QString& id : subfolderIds()) s.replace("${" + id + "}", subfolder(id));
    return s;
}

QString GameFolder::saveStatesFolder() const { return QDir::cleanPath(expand(manifest_->saveStatesFolder())); }

QStringList GameFolder::runArgs(const QString& isoPath, const QString& statePath) const {
    QStringList out;
    for (const QString& a : manifest_->runArgs()) out << expand(a, isoPath, statePath);
    if (!statePath.isEmpty()) for (const QString& a : manifest_->resumeArgs()) out << expand(a, isoPath, statePath);
    return out;
}

QString GameFolder::preparedRecordPath() const { return path() + "/prepared.json"; }   // lint: allow

PreparedRecord GameFolder::prepared() const {
    PreparedRecord r;
    QFile f(preparedRecordPath());
    if (!f.open(QIODevice::ReadOnly)) return r;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    r.discSha256 = o.value("disc_sha256").toString();
    r.discPath = o.value("disc").toString();
    r.targetId = o.value("target").toString();
    r.binaryPath = o.value("binary").toString();
    r.launcherVersion = o.value("launcher_version").toString();
    r.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODate);
    r.ok = !r.binaryPath.isEmpty() && QFileInfo::exists(r.binaryPath);
    return r;
}

void GameFolder::recordPrepared(const PreparedRecord& r) const {
    QJsonObject o;
    o["disc_sha256"] = r.discSha256;
    o["disc"] = r.discPath;
    o["target"] = r.targetId;
    o["binary"] = r.binaryPath;
    o["launcher_version"] = r.launcherVersion;
    o["when"] = (r.when.isValid() ? r.when : QDateTime::currentDateTime()).toString(Qt::ISODate);
    QDir().mkpath(path());
    QSaveFile f(preparedRecordPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

bool GameFolder::upToDate(const QString& discSha256, const QString& launcherVersion) const {
    const PreparedRecord r = prepared();
    return r.ok && r.discSha256 == discSha256 && r.launcherVersion == launcherVersion && r.targetId == HostPlatform::targetId(*manifest_);
}

}  // namespace ps2r::launcher
