// app/launcher/games_page.h -- Games: the cards (one per disc the player added, the unsupported ones badged), the
// empty and error states and "Add a game…" (the setup window); the whole page is a drop zone. A card's action plays
// through LauncherApp, or opens the setup window when the game is not built yet.
#pragma once
#include <QWidget>
namespace ps2r::launcher {
class DetailsSheet;
class DiscList;
class FlatButton;
class LauncherApp;

class GamesPage : public QWidget {
    Q_OBJECT
public:
    GamesPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    DiscList* discs() const { return discs_; }
    // The card's action, as a method so the test driver and the button share it.
    void runCardAction(const QString& isoPath);

signals:
    void setupRequested(const QString& isoPath);   // the window opens the setup window (for this disc when not empty)

private:
    LauncherApp* app_;
    DetailsSheet* details_;
    DiscList* discs_;
};

}  // namespace ps2r::launcher
