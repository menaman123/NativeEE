// app/launcher/settings_page.h -- Settings: the manifest's groups and rows mirroring the game's options file in plain
// language, read when shown and written one key at a time; the memory-card folder row; the note on where the file is.
// The frame-rate row's "uncapped" locks the vertical-sync row and explains why, as the game's own menu does.
#pragma once
#include <QVector>
#include <QWidget>
namespace ps2r::launcher {
class FolderRow;
class LauncherApp;
class SettingsRow;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(LauncherApp* app, QWidget* parent = nullptr);
    void reload();   // reads every row from the options file
    SettingsRow* row(const QString& key) const;
    const QVector<SettingsRow*>& rows() const { return rows_; }
    FolderRow* memoryCardsRow() const { return memoryCards_; }
    FolderRow* gameFolderRow() const { return gameFolder_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyDependencies();
    LauncherApp* app_;
    QVector<SettingsRow*> rows_;
    FolderRow* memoryCards_ = nullptr;
    FolderRow* gameFolder_ = nullptr;
};

}  // namespace ps2r::launcher
