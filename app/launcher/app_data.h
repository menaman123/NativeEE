// app/launcher/app_data.h -- the launcher's own folder in the user's app-data (macOS ~/Library/Application
// Support/<product>, Windows %LOCALAPPDATA%\<product>, Linux ~/.local/share/<product>): where everything the launcher
// installs and builds lives, never system-wide. Also the launcher's state file (launcher.json): the folder choices,
// the window, the per-game facts. One instance per running launcher.
#pragma once
#include <QJsonObject>
#include <QString>
namespace ps2r::launcher {

class AppData {
public:
    explicit AppData(const QString& productFolder);

    QString root() const { return root_; }
    QString toolchainDir() const;   // Toolchain/: cmake, ninja, wasm32 (the title's pinned fetch), ffmpeg, python, vitasdk
    QString sourcesDir() const;     // Sources/<title id>/
    QString buildsDir() const;      // Builds/<title id>/<target>/
    QString logsDir() const;        // Logs/
    QString downloadsDir() const;   // Downloads/ (the archives, kept so a re-run verifies instead of fetching)
    QString defaultGamesDir() const;
    QString defaultModsDir() const;
    QString sourceDir(const QString& titleId) const;
    QString buildDir(const QString& titleId, const QString& targetId) const;
    QString logPath(const QString& step) const;   // Logs/<step>-<timestamp>.log

    // Expands ~, ${app_data}, ${games} in a manifest path.
    QString expand(const QString& path, const QString& gamesDir = QString()) const;
    static QString homePath();
    static QString shortPath(const QString& path);   // ~/… for display

    bool ensureFolders(QString* error) const;

    // launcher.json
    QJsonObject state() const { return state_; }
    QJsonValue value(const QString& key, const QJsonValue& fallback = QJsonValue()) const;
    void setValue(const QString& key, const QJsonValue& value);
    bool save(QString* error = nullptr) const;
    QString statePath() const;

private:
    void loadState();
    QString root_;
    QJsonObject state_;
};

}  // namespace ps2r::launcher
