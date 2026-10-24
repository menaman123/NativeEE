// app/launcher/game_folder.h -- where the game lives: ONE folder the user chose, holding the executable
// (<folder>/<binary>), the options file (<folder>/nativeee.ini) and the sub-folders (memcards/, saves/, cache/ --
// each one changeable later). Portable: move the folder and the game moves with it. Also the record of what was
// prepared into it (prepared.json: the disc's checksum, the platform, the launcher version), which decides whether
// Play or Prepare is the next step.
#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
namespace ps2r::launcher {
class AppData;
class TitleManifest;

struct PreparedRecord {
    bool ok = false;
    QString discSha256;      // the executable's checksum of the disc it was prepared from
    QString discPath;
    QString targetId;
    QString binaryPath;
    QString launcherVersion;
    QDateTime when;
};

class GameFolder {
public:
    GameFolder(AppData* appData, const TitleManifest* manifest);

    bool chosen() const;
    QString path() const;                    // empty until chosen
    void choose(const QString& folder);      // remembered in launcher.json; the sub-folders are made on prepare
    QString defaultParent() const;           // where the picker opens: the Desktop
    // The folder the picker returns, made into the game folder: a "<name>" folder inside unless the pick is one already.
    QString folderFor(const QString& picked) const;

    QString configPath() const;              // <folder>/nativeee.ini
    QString binaryPath(const QString& targetId = QString()) const;   // <folder>/<target binary>; .app -> its executable
    QString subfolder(const QString& id) const;   // memcards / saves / cache: the override or <folder>/<default>
    void setSubfolder(const QString& id, const QString& path);
    QStringList subfolderIds() const;
    bool makeSubfolders(QString* error) const;
    QString saveStatesFolder() const;        // run.save_states.folder with ${saves} etc. substituted

    // The run arguments with ${iso} ${config} ${memcards} ${saves} ${cache} ${folder} ${state} substituted.
    QStringList runArgs(const QString& isoPath, const QString& statePath = QString()) const;
    QString expand(const QString& text, const QString& isoPath = QString(), const QString& statePath = QString()) const;

    PreparedRecord prepared() const;
    void recordPrepared(const PreparedRecord& r) const;
    // Up to date = prepared for this launcher version from the disc with this checksum, and the executable exists.
    bool upToDate(const QString& discSha256, const QString& launcherVersion) const;
    QString preparedRecordPath() const;

    static QString executableInside(const QString& binaryPath);   // an .app bundle's Contents/MacOS/<name>, else itself
    static QString safeFolderName(const QString& name);           // the title's name as a folder name every file system takes

private:
    AppData* appData_;
    const TitleManifest* manifest_;
};

}  // namespace ps2r::launcher
