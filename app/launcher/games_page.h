// app/launcher/games_page.h -- Games: the cards (one per disc found, the unsupported ones badged), the empty and
// error states, Add a disc image…, Search again and "Prepare for another computer…"; the whole page is a drop zone.
// A card's action prepares or plays through LauncherApp; while the game is prepared the first run's Preparing page
// shows the one progress bar.
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
    FlatButton* otherComputerButton() const { return other_; }
    // The card's action, as a method so the test driver and the button share it.
    void runCardAction(const QString& isoPath);

signals:
    void prepareRequested(const QString& isoPath);   // the window opens the first run's Preparing page
    void folderRequested();                          // the window opens the first run at "Where should the game live?"
    void otherComputerRequested();                   // the window opens the first run at "Which computer is it for?"

private:
    LauncherApp* app_;
    DetailsSheet* details_;
    DiscList* discs_;
    FlatButton* other_;
};

}  // namespace ps2r::launcher
