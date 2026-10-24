// app/launcher/app_data.h -- the launcher's own folder in the user's app-data (macOS ~/Library/Application
// Support/<product>, Windows %LOCALAPPDATA%\<product>, Linux ~/.local/share/<product>): the launcher's state file
// (launcher.json: the game folder, the discs, the window) and its logs, never system-wide. The game itself lives in
// the folder the user chose (GameFolder). One instance per running launcher.
#pragma once
#include <QJsonObject>
#include <QString>
namespace ps2r::launcher {

class AppData {
public:
    explicit AppData(const QString& productFolder);

    QString root() const { return root_; }
    QString logsDir() const;        // Logs/
    QString logPath(const QString& step) const;   // Logs/<step>-<timestamp>.log

    // Expands ~ and ${app_data} in a manifest path.
    QString expand(const QString& path) const;
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
