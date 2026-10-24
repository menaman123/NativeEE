// app/launcher/main_window.h -- the window: the setup window (one page: the disc, the folder, the settings, Build the
// game) fills it until the game is built (decision D-L10); then the sidebar at the left and the four pages (Games, Save
// States, Settings, About) in a stack, the Details sheet over the content. The setup window comes back from Games >
// "Add a game…" and from a folder change on Settings. A screenshot action (Cmd/Ctrl+Shift+S) writes the window to Logs/. Remembers its size and page in
// launcher.json. Never smaller than 1024x640.
#pragma once
#include <QMainWindow>
class QStackedWidget;
namespace ps2r::launcher {
class AboutPage;
class DetailsSheet;
class GamesPage;
class LauncherApp;
class SaveStatesPage;
class SettingsPage;
class SetupPage;
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
    SetupPage* setup() const { return setup_; }
    DetailsSheet* detailsSheet() const { return details_; }
    Sidebar* sidebar() const { return sidebar_; }
    void showPage(int index);
    int currentPage() const;
    bool setupShowing() const;
    void showSetup(const QString& isoPath = QString());
    void showPages();
    // Writes a PNG of the window; returns its path.
    QString screenshot(const QString& path = QString());
    // Opens the setup window when the game is not built yet; false when nothing was needed (a returning player lands
    // on Games).
    bool startSetupIfNeeded();

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
    SetupPage* setup_;
};

}  // namespace ps2r::launcher
