// app/launcher/game_folder.h -- where the game lives: ONE folder the user chose, holding the executable
// (<folder>/<binary>), the options file (<folder>/nativeee.ini) and the sub-folders (memcards/, saves/, cache/ --
// the first two the player's choice on the setup window, each changeable later; their places are the options file's
// own paths.* keys, the ones the game reads). Portable: move the folder and the game moves with it. Also the record of
// what was prepared into it (prepared.json: the disc's checksum, the platform, the launcher version), which decides
// whether Play or Prepare is the next step.
#pragma once
#include <QDateTime>
#include <QMap>
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
    QString defaultParent() const;           // where the picker opens: the parent of the suggested folder
    // The folder suggested on the setup window: game_folder.default from the manifest (~ expanded), else
    // ~/Documents/<the title's name made safe>. Under the HOME AppData uses, so a test never writes into the real one.
    QString suggestedPath() const;
    // The folder the picker returns, made into the game folder: a "<name>" folder inside unless the pick is one already.
    QString folderFor(const QString& picked) const;

    QString configPath() const;              // <folder>/nativeee.ini
    QString binaryPath(const QString& targetId = QString()) const;   // <folder>/<target binary>; .app -> its executable

    // The sub-folders (memcards / saves / cache). ONE store: the options-file key the GAME reads for each
    // (game_folder.subfolders.<id>.key -- paths.mc_dir, paths.save_dir, paths.user_data), written as an absolute path
    // whenever the game folder is chosen, when the game is built and again after the pipeline rewrote the file
    // (D-L13). Before a game folder exists a choice waits here and lands in the file with the first write. The rule
    // (D-L15): a path INSIDE the game folder is kept relative ("memcards") and follows the game folder when that
    // changes; a path outside stays where it is. A sub-folder without a key is fixed at <folder>/<default>.
    QString subfolder(const QString& id) const;                       // absolute: the choice resolved against the game folder
    QString subfolderIn(const QString& id, const QString& folder) const;   // ...against another folder (the suggested one)
    QString subfolderChoice(const QString& id) const;                 // what the row shows: "memcards" (inside) or an absolute path
    bool subfolderInside(const QString& id) const;                    // the choice is relative = inside the game folder
    QString subfolderKey(const QString& id) const;                    // the options-file key; empty = not written, not changeable
    void setSubfolder(const QString& id, const QString& path);        // chooses; writes the key when a game folder is chosen
    QMap<QString, QString> subfolderChoices() const;                  // every id's choice (what Build keeps across the pipeline's rewrite)
    void setSubfolderChoices(const QMap<QString, QString>& choices);  // ...and puts them back (writing the keys)
    bool writeSubfolderKeys(QString* error = nullptr);                // every keyed sub-folder's absolute path into the options file
    static QString relativeIfInside(const QString& path, const QString& folder);   // "memcards" for <folder>/memcards, else `path`
    static QString resolveIn(const QString& choice, const QString& folder);        // the absolute path of a choice
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
    QString storedSubfolder(const QString& id) const;   // the options file's value for the id's key, else empty
    AppData* appData_;
    const TitleManifest* manifest_;
    QMap<QString, QString> pending_;   // id -> choice (relative or absolute) not yet in the options file
};

}  // namespace ps2r::launcher
