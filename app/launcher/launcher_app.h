// app/launcher/launcher_app.h -- the launcher's model: the manifest, the app-data folder, the discs the player chose,
// the game folder, the prepare runner and the running game, wired together; the window shows it and the test driver
// drives it through the same methods the buttons call. The launcher never looks for disc images by itself (decision
// D-L10): a disc enters through addDisc() -- the file dialog, a drop, or the command line -- and is identified by content.
#pragma once
#include <QMap>
#include <QObject>
#include <QStringList>
#include "disc_identity.h"
namespace ps2r::launcher {
class AppData;
class GameFolder;
class GameLibrary;
class GameProcess;
class IniSettings;
class PrepareRunner;
class TitleManifest;

class LauncherApp : public QObject {
    Q_OBJECT
public:
    explicit LauncherApp(QObject* parent = nullptr);
    ~LauncherApp() override;

    // Finds the manifest: an explicit path, else PS2R_LAUNCHER_MANIFEST, else titles/*.json beside the application
    // (Contents/Resources/titles on macOS). False with *error when none is found.
    bool loadManifest(const QString& explicitPath, QString* error);
    static QStringList manifestSearchPaths();

    const TitleManifest* manifest() const { return manifest_; }
    AppData* appData() const { return appData_; }
    GameLibrary* library() const { return library_; }
    GameFolder* gameFolder() const { return folder_; }
    PrepareRunner* preparer() const { return preparer_; }
    GameProcess* game() const { return game_; }

    // the game folder and its sub-folders; `exact` takes the path as the game folder itself (a scripted run), else the
    // picker's choice is made into one (GameFolder::folderFor). A sub-folder choice is the options file's own key
    // (paths.mc_dir / paths.save_dir, the ones the game reads -- GameFolder): the setup window, the Settings page and
    // the game share ONE store.
    void chooseGameFolder(const QString& pickedOrFolder, bool exact = false);
    void setSubfolder(const QString& id, const QString& path);
    QString settingsFilePath() const;
    IniSettings* settings() const { return settings_; }
    QString settingValue(const QString& key) const;   // the file's value or the manifest's default
    bool setSettingValue(const QString& key, const QString& value, QString* error = nullptr);

    // discs: identifies a dropped / chosen file by its content; a recognised disc (supported or not) joins the library.
    DiscIdentity addDisc(const QString& path);
    // The Build rule: true when `isoPath` is a known, present, supported disc and nothing is being prepared; else *why
    // says what is missing in plain words.
    bool canPrepare(const QString& isoPath, QString* why = nullptr) const;
    // The supported disc to prepare / play: the one chosen for the first run, else the first supported one found.
    QString currentDisc() const;
    void setCurrentDisc(const QString& isoPath);

    // what a card offers next
    enum Action { Prepare, Play, Playing, Missing, Unsupported, NoFolder };
    Action nextAction(const QString& isoPath) const;
    QString hostTarget() const;
    // Starts preparing `isoPath` for `targetId` (empty = this machine) into the game folder; false with *error when it
    // cannot start. Progress / failed / finished come from preparer().
    bool prepare(const QString& isoPath, const QString& targetId, QString* error);
    bool preparing() const;
    bool play(const QString& isoPath, QString* error, const QString& statePath = QString());
    void stopGame();
    // The first run is complete when a game folder is chosen and the game is prepared in it for this machine.
    bool setupComplete() const;
    // After the launcher was updated: the game folder holds the game an older launcher put there. When this
    // launcher carries the game ready-made, it is copied in again for the same disc and computer (a few seconds,
    // shown on the disc's card); true when that started. A rebuild by the prepare pipeline stays the player's choice.
    bool refreshGame();

    QString version() const;
    QString engineCommit() const;

signals:
    void foldersChanged();
    void settingsChanged(const QString& key);
    void stateChanged();   // anything a card shows
    void prepared(const QString& binaryPath);

private:
    void openSettings();
    TitleManifest* manifest_ = nullptr;
    AppData* appData_ = nullptr;
    GameLibrary* library_ = nullptr;
    GameFolder* folder_ = nullptr;
    PrepareRunner* preparer_ = nullptr;
    GameProcess* game_ = nullptr;
    IniSettings* settings_ = nullptr;
    QString playingIso_, preparingIso_, preparingTarget_, preparingSha_;
    QMap<QString, QString> subfoldersAtBuild_;   // the sub-folder choices when Build was pressed (re-applied after the pipeline)
};

}  // namespace ps2r::launcher
