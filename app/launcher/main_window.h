// app/launcher/main_window.h -- the window: the sidebar at the left, the four pages in a stack, the sheets (first
// run, Details) over the content; the first run opens itself when the installer is not complete; a screenshot action
// (Cmd/Ctrl+Shift+S) writes the window to Logs/ for the documentation. Remembers its size and page in launcher.json.
#pragma once
#include <QMainWindow>
class QStackedWidget;
namespace ps2r::launcher {
class AboutPage;
class DetailsSheet;
class FirstRunSheet;
class GamesPage;
class LauncherApp;
class SettingsPage;
class Sidebar;
class TargetsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(LauncherApp* app, QWidget* parent = nullptr);
    ~MainWindow() override;

    GamesPage* gamesPage() const { return games_; }
    SettingsPage* settingsPage() const { return settings_; }
    TargetsPage* targetsPage() const { return targets_; }
    AboutPage* aboutPage() const { return about_; }
    FirstRunSheet* firstRunSheet() const { return firstRun_; }
    DetailsSheet* detailsSheet() const { return details_; }
    Sidebar* sidebar() const { return sidebar_; }
    void showPage(int index);
    int currentPage() const;
    // Writes a PNG of the window; returns its path.
    QString screenshot(const QString& path = QString());
    // Opens the first-run sheet and starts the installer when it is not complete; false when nothing was needed.
    bool startFirstRunIfNeeded();
    void askGameFolderIfNeeded();   // the first question: where the finished game is installed

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    LauncherApp* app_;
    Sidebar* sidebar_;
    QStackedWidget* stack_;
    QWidget* content_;
    GamesPage* games_;
    SettingsPage* settings_;
    TargetsPage* targets_;
    AboutPage* about_;
    DetailsSheet* details_;
    FirstRunSheet* firstRun_;
};

}  // namespace ps2r::launcher
