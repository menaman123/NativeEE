// app/launcher/main_window.h -- the window: the first run (five pages) fills it until the game is prepared; then the
// sidebar at the left and the four pages (Games, Save States, Settings, About) in a stack, the Details sheet over the
// content. A screenshot action (Cmd/Ctrl+Shift+S) writes the window to Logs/. Remembers its size and page in
// launcher.json. Never smaller than 1024x640.
#pragma once
#include <QMainWindow>
class QStackedWidget;
namespace ps2r::launcher {
class AboutPage;
class DetailsSheet;
class FirstRunFlow;
class GamesPage;
class LauncherApp;
class SaveStatesPage;
class SettingsPage;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum PageIndex { GamesIndex, SaveStatesIndex, SettingsIndex, AboutIndex };
    explicit MainWindow(LauncherApp* app, QWidget* parent = nullptr);
    ~MainWindow() override;

    GamesPage* gamesPage() const { return games_; }
    SaveStatesPage* saveStatesPage() const { return saves_; }
    SettingsPage* settingsPage() const { return settings_; }
    AboutPage* aboutPage() const { return about_; }
    FirstRunFlow* firstRun() const { return firstRun_; }
    DetailsSheet* detailsSheet() const { return details_; }
    Sidebar* sidebar() const { return sidebar_; }
    void showPage(int index);
    int currentPage() const;
    bool firstRunShowing() const;
    void showFirstRun(int page = 0);   // FirstRunFlow::Page
    void showPages();
    // Writes a PNG of the window; returns its path.
    QString screenshot(const QString& path = QString());
    // Opens the first run when the game is not prepared yet; false when nothing was needed.
    bool startFirstRunIfNeeded();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    LauncherApp* app_;
    QStackedWidget* root_;
    QWidget* pagesArea_;
    Sidebar* sidebar_;
    QStackedWidget* stack_;
    QWidget* content_;
    GamesPage* games_;
    SaveStatesPage* saves_;
    SettingsPage* settings_;
    AboutPage* about_;
    DetailsSheet* details_;
    FirstRunFlow* firstRun_;
};

}  // namespace ps2r::launcher
