// app/launcher/settings_page.h -- Settings: the manifest's groups and rows mirroring the game's options file in plain
// language, read when shown and written one key at a time; the Folders group (the game folder and each sub-folder,
// changeable); the note on where the file is. A row's `locked_by` rule (from the manifest) locks it and says why
// while another row has a given value. Without a game folder the page is one empty state.
#pragma once
#include <QVector>
#include <QWidget>
namespace ps2r::launcher {
class EmptyState;
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
    FolderRow* folderRow(const QString& id) const;   // "game" or a sub-folder id
    EmptyState* emptyState() const { return empty_; }

signals:
    void folderRequested();   // the window opens the first run at "Where should the game live?"

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyLocks();
    void refreshFolders();
    LauncherApp* app_;
    QVector<SettingsRow*> rows_;
    QVector<FolderRow*> folders_;
    EmptyState* empty_;
    QWidget* body_;
};

}  // namespace ps2r::launcher
