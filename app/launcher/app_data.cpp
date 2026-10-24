// app/launcher/app_data.cpp -- the app-data folder and launcher.json.
#include "app_data.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSaveFile>
namespace ps2r::launcher {

QString AppData::homePath() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    const QString p = env.value("USERPROFILE");
#else
    const QString p = env.value("HOME");
#endif
    return p.isEmpty() ? QDir::homePath() : QDir::cleanPath(p);
}

AppData::AppData(const QString& productFolder) {
#ifdef Q_OS_MACOS
    root_ = homePath() + "/Library/Application Support/" + productFolder;
#elif defined(Q_OS_WIN)
    QString local = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
    if (local.isEmpty()) local = homePath() + "/AppData/Local";
    root_ = QDir::cleanPath(local) + "/" + productFolder;
#else
    root_ = homePath() + "/.local/share/" + productFolder;
#endif
    loadState();
}

QString AppData::logsDir() const { return root_ + "/Logs"; }
QString AppData::statePath() const { return root_ + "/launcher.json"; }   // lint: allow

QString AppData::logPath(const QString& step) const {
    return logsDir() + "/" + step + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".log";
}

QString AppData::expand(const QString& path) const {
    QString p = path;
    p.replace("${app_data}", root_);
    if (p == "~") p = homePath();
    else if (p.startsWith("~/")) p = homePath() + p.mid(1);
    return QDir::cleanPath(p);
}

QString AppData::shortPath(const QString& path) {
    const QString home = homePath();
    if (path == home) return "~";
    if (path.startsWith(home + "/")) return "~" + path.mid(home.size());
    return path;
}

bool AppData::ensureFolders(QString* error) const {
    for (const QString& d : {root_, logsDir()}) {
        if (!QDir().mkpath(d)) {
            if (error) *error = QStringLiteral("cannot create %1").arg(d);
            return false;
        }
    }
    return true;
}

void AppData::loadState() {
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) state_ = doc.object();
}

QJsonValue AppData::value(const QString& key, const QJsonValue& fallback) const {
    return state_.contains(key) ? state_.value(key) : fallback;
}

void AppData::setValue(const QString& key, const QJsonValue& value) { state_.insert(key, value); }

bool AppData::save(QString* error) const {
    if (!QDir().mkpath(root_)) {
        if (error) *error = QStringLiteral("cannot create %1").arg(root_);
        return false;
    }
    QSaveFile f(statePath());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(statePath());
        return false;
    }
    f.write(QJsonDocument(state_).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(statePath());
        return false;
    }
    return true;
}

}  // namespace ps2r::launcher
