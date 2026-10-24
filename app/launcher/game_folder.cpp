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
#include "title_manifest.h"
namespace ps2r::launcher {

GameFolder::GameFolder(AppData* appData, const TitleManifest* manifest) : appData_(appData), manifest_(manifest) {}

bool GameFolder::chosen() const { return !path().isEmpty(); }
QString GameFolder::path() const { return appData_->value("game_folder").toString(); }

void GameFolder::choose(const QString& folder) {
    appData_->setValue("game_folder", QDir::cleanPath(folder));
    appData_->save();
}

QString GameFolder::defaultParent() const {
    // the Desktop under the home AppData uses (the HOME variable), so a test or a portable run with another HOME
    // never writes into the real user's Desktop
    const QString desktop = AppData::homePath() + "/Desktop";
    return QDir(desktop).exists() ? desktop : AppData::homePath();
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

QString GameFolder::subfolder(const QString& id) const {
    const QString chosen = appData_->value("subfolder." + id).toString();
    if (!chosen.isEmpty()) return chosen;
    for (const SubfolderSpec& s : manifest_->subfolders()) if (s.id == id) return path() + "/" + s.defaultName;
    return path() + "/" + id;
}

void GameFolder::setSubfolder(const QString& id, const QString& folder) {
    appData_->setValue("subfolder." + id, QDir::cleanPath(folder));
    appData_->save();
    QDir().mkpath(folder);
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
