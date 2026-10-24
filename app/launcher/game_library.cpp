// app/launcher/game_library.cpp -- the known discs and their facts.
#include "game_library.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include "app_data.h"
namespace ps2r::launcher {

GameLibrary::GameLibrary(AppData* appData, QObject* parent) : QObject(parent), appData_(appData) { load(); }

void GameLibrary::load() {
    games_.clear();
    for (const QJsonValue& v : appData_->value("games").toArray()) {
        const QJsonObject o = v.toObject();
        GameEntry e;
        e.path = o.value("path").toString();
        e.fingerprintFile = o.value("fingerprint").toString();
        e.sizeBytes = qint64(o.value("size").toDouble());
        e.identity.path = e.path;
        e.identity.fingerprintFile = e.fingerprintFile;
        e.identity.status = DiscIdentity::statusFromKey(o.value("status").toString(QStringLiteral("supported")));
        e.identity.region = o.value("region").toString();
        e.identity.regionLabel = o.value("region_label").toString();
        e.identity.id = o.value("id").toString();
        e.identity.sha256 = o.value("sha256").toString();   // lint: allow
        e.identity.reason = o.value("reason").toString();
        e.identity.sizeBytes = e.sizeBytes;
        e.lastPlayed = QDateTime::fromString(o.value("last_played").toString(), Qt::ISODate);
        e.playtimeSeconds = qint64(o.value("playtime_seconds").toDouble());
        if (!e.path.isEmpty()) games_ << e;
    }
    refreshMissing();
}

void GameLibrary::save() {
    QJsonArray arr;
    for (const GameEntry& e : games_) {
        QJsonObject o;
        o["path"] = e.path;
        o["fingerprint"] = e.fingerprintFile;
        o["size"] = double(e.sizeBytes);
        o["status"] = DiscIdentity::statusKey(e.identity.status);
        o["region"] = e.identity.region;
        o["region_label"] = e.identity.regionLabel;
        o["id"] = e.identity.id;
        o["sha256"] = e.identity.sha256;   // lint: allow
        o["reason"] = e.identity.reason;
        if (e.lastPlayed.isValid()) o["last_played"] = e.lastPlayed.toString(Qt::ISODate);
        o["playtime_seconds"] = double(e.playtimeSeconds);
        arr << o;
    }
    appData_->setValue("games", arr);
    appData_->save();
}

int GameLibrary::indexOf(const QString& path) const {
    for (int i = 0; i < games_.size(); ++i) if (games_[i].path == path) return i;
    return -1;
}

const GameEntry* GameLibrary::game(const QString& path) const {
    const int i = indexOf(path);
    return i < 0 ? nullptr : &games_[i];
}

bool GameLibrary::add(const DiscIdentity& identity) {
    if (!identity.recognised()) return false;
    const int i = indexOf(identity.path);
    if (i >= 0) {
        games_[i].fingerprintFile = identity.fingerprintFile;
        games_[i].identity = identity;
        games_[i].sizeBytes = identity.sizeBytes;
        games_[i].missing = false;
        save();
        emit changed();
        return false;
    }
    GameEntry e;
    e.path = identity.path;
    e.fingerprintFile = identity.fingerprintFile;
    e.identity = identity;
    e.sizeBytes = identity.sizeBytes;
    games_ << e;
    save();
    emit changed();
    return true;
}

void GameLibrary::remove(const QString& path) {
    const int i = indexOf(path);
    if (i < 0) return;
    games_.remove(i);
    save();
    emit changed();
}

void GameLibrary::recordPlayStart(const QString& path) {
    const int i = indexOf(path);
    if (i < 0) return;
    games_[i].lastPlayed = QDateTime::currentDateTime();
    save();
    emit changed();
}

void GameLibrary::recordPlayEnd(const QString& path, qint64 seconds) {
    const int i = indexOf(path);
    if (i < 0) return;
    games_[i].playtimeSeconds += qMax<qint64>(0, seconds);
    save();
    emit changed();
}

void GameLibrary::refreshMissing() {
    for (GameEntry& e : games_) e.missing = !QFileInfo::exists(e.path);
}

QString GameLibrary::relativeTime(const QDateTime& when, const QDateTime& now) {
    if (!when.isValid()) return QStringLiteral("Never played");
    const qint64 secs = when.secsTo(now);
    if (secs < 60) return QStringLiteral("Last played just now");
    if (secs < 3600) return QStringLiteral("Last played %1 min ago").arg(secs / 60);
    if (when.date() == now.date()) return QStringLiteral("Last played today %1").arg(when.toString("HH:mm"));
    if (when.date().daysTo(now.date()) == 1) return QStringLiteral("Last played yesterday %1").arg(when.toString("HH:mm"));
    if (when.date().daysTo(now.date()) < 7) return QStringLiteral("Last played %1").arg(when.toString("dddd HH:mm"));
    return QStringLiteral("Last played %1").arg(when.toString("yyyy-MM-dd"));
}

QString GameLibrary::playtimeText(qint64 seconds) {
    if (seconds < 60) return QStringLiteral("under a minute");
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h == 0) return QStringLiteral("%1 min").arg(m);
    return QStringLiteral("%1 h %2 min").arg(h).arg(m);
}

}  // namespace ps2r::launcher
