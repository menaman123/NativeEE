// app/launcher/games_page.h -- Games: the cards (one per disc found), the empty and error states, the Folders rows
// (Games / Mods / Memory cards), Add a disc image… and Rescan; the whole page is a drop zone. Its cards' actions run
// the installer, the build or the game through LauncherApp.
#pragma once
#include <QVector>
#include <QWidget>
class QLabel;
class QScrollArea;
class QVBoxLayout;
namespace ps2r::launcher {
class DetailsSheet;
class EmptyState;
class FirstRunSheet;
class FlatButton;
class FolderRow;
class GameCard;
class LauncherApp;

class GamesPage : public QWidget {
    Q_OBJECT
public:
    GamesPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    void setFirstRunSheet(FirstRunSheet* sheet) { firstRun_ = sheet; }
    void rebuildCards();
    GameCard* card(const QString& isoPath) const;
    const QVector<GameCard*>& cards() const { return cards_; }
    EmptyState* emptyState() const { return empty_; }
    FlatButton* addButton() const { return add_; }
    FlatButton* rescanButton() const { return rescan_; }
    FolderRow* folderRow(const QString& id) const;
    // The card's action, as a method so the test driver and the button share it.
    void runCardAction(const QString& isoPath);
    void showDiscError(const QString& reason);
    void addDiscFromPath(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void chooseDisc();
    void changeFolder(const QString& id);
    void refreshFolders();
    void refreshCards();
    LauncherApp* app_;
    DetailsSheet* details_;
    FirstRunSheet* firstRun_ = nullptr;
    QScrollArea* scroll_;
    QWidget* cardsHost_;
    QVBoxLayout* cardsLayout_;
    EmptyState* empty_;
    QVector<GameCard*> cards_;
    QVector<FolderRow*> folders_;
    FlatButton* add_;
    FlatButton* rescan_;
    QLabel* scanNote_;
    bool dragOver_ = false;
};

}  // namespace ps2r::launcher
