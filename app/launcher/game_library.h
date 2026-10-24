// app/launcher/game_library.h -- the discs the launcher knows (found by a scan, dropped or chosen) with the facts a
// card shows: last played, playtime, what the disc is (region, supported or not). Persisted in launcher.json through AppData.
#pragma once
#include <QDateTime>
#include <QObject>
#include <QVector>
#include "disc_identity.h"
namespace ps2r::launcher {
class AppData;

struct GameEntry {
    QString path;
    QString fingerprintFile;
    DiscIdentity identity;   // what the disc is (status, region, id, checksum) as last read
    qint64 sizeBytes = 0;
    QDateTime lastPlayed;
    qint64 playtimeSeconds = 0;
    bool missing = false;   // the file is gone since the last start
};

class GameLibrary : public QObject {
    Q_OBJECT
public:
    explicit GameLibrary(AppData* appData, QObject* parent = nullptr);

    const QVector<GameEntry>& games() const { return games_; }
    int indexOf(const QString& path) const;
    const GameEntry* game(const QString& path) const;
    // Adds or refreshes an entry from its identity (recognised discs only); returns true when it is new.
    bool add(const DiscIdentity& identity);
    void remove(const QString& path);
    void recordPlayStart(const QString& path);
    void recordPlayEnd(const QString& path, qint64 seconds);
    void refreshMissing();
    void save();

    static QString relativeTime(const QDateTime& when, const QDateTime& now = QDateTime::currentDateTime());
    static QString playtimeText(qint64 seconds);

signals:
    void changed();

private:
    void load();
    AppData* appData_;
    QVector<GameEntry> games_;
};

}  // namespace ps2r::launcher
