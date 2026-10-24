// app/launcher/first_run_flow.h -- the first run, five pages in one frame: 1 "Where is your game?" (the discs found,
// dropped or chosen; one card per image; the unsupported ones badged, never refused silently), 2 "Where should the
// game live?" (one folder), 3 "Which computer is it for?" (this one preselected), 4 "Preparing your game" (ONE
// progress bar with an honest estimate; a plain error with Retry and Details), 5 "Ready to play". Shown instead of the
// pages until the game is prepared; reopenable at page 3 to prepare for another computer.
#pragma once
#include <QWidget>
class QLabel;
class QStackedWidget;
namespace ps2r::launcher {
class DetailsSheet;
class DiscList;
class FlatButton;
class FolderRow;
class LauncherApp;
class PageHeader;
class ProgressPanel;
class SegmentedControl;

class FirstRunFlow : public QWidget {
    Q_OBJECT
public:
    enum Page { WhereIsTheGame, WhereItLives, WhichComputer, Preparing, Ready };
    FirstRunFlow(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    void open(Page page = WhereIsTheGame);
    Page currentPage() const;
    void showPage(Page page);
    // the buttons, as methods so the test driver and the clicks share them
    void next();
    void back();
    void startPreparing();
    void cancelPreparing();
    void play();
    DiscList* discList() const { return discs_; }
    FolderRow* folderRow() const { return folder_; }
    SegmentedControl* targetSelector() const { return targets_; }
    ProgressPanel* progress() const { return progress_; }
    FlatButton* nextButton() const { return next_; }
    FlatButton* backButton() const { return back_; }
    FlatButton* retryButton() const { return retry_; }
    FlatButton* detailsButton() const { return details_; }
    FlatButton* cancelButton() const { return cancel_; }
    FlatButton* playButton() const { return play_; }
    FlatButton* laterButton() const { return later_; }
    QString errorText() const;
    QString selectedTarget() const;

signals:
    void completed();   // the game plays, or the user left the flow for the pages

private:
    QWidget* buildWhereIsTheGame();
    QWidget* buildWhereItLives();
    QWidget* buildWhichComputer();
    QWidget* buildPreparing();
    QWidget* buildReady();
    void refreshButtons();
    void refreshFolderPage();
    void refreshTargetPage();
    void refreshReadyPage();
    void setError(const QString& text);
    LauncherApp* app_;
    DetailsSheet* detailsSheet_;
    QLabel* step_;
    QStackedWidget* stack_;
    DiscList* discs_;
    FolderRow* folder_;
    QLabel* layoutPreview_;
    QLabel* portableNote_;
    SegmentedControl* targets_;
    QLabel* targetNote_;
    ProgressPanel* progress_;
    QLabel* prepareIntro_;
    QLabel* error_;
    FlatButton* retry_;
    FlatButton* details_;
    FlatButton* cancel_;
    QLabel* readyTitle_;
    QLabel* readyBody_;
    FlatButton* play_;
    FlatButton* openFolder_;
    FlatButton* later_;
    FlatButton* back_;
    FlatButton* next_;
    QString suggestedFolder_;
    QString errorText_, lastLog_;
    QStringList targetIds_;
    qint64 etaSeconds_ = -1;
};

}  // namespace ps2r::launcher
