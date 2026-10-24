// app/launcher/setup_page.h -- the setup window (decision D-L10): ONE page the launcher opens with on the first run, and
// again from Games > "Add a game…" or a Settings folder change. Top to bottom: "Your game" (a path field, Choose…, a drop
// anywhere on the window; the chosen file is identified by its content and shown as a card -- Supported, or "Not supported
// yet" with the plain reason -- never looked for by itself), "Where the game lives" (one folder, Documents by default,
// Change…; under it the memory cards and the save states -- inside the game folder unless the player chooses somewhere
// else; their places are the options file's own paths.* keys, written when the game is built -- D-L15), "Settings"
// (the manifest's rows, kept here and written to <folder>/nativeee.ini when the game is built),
// "Advanced" (which computer; this one preselected), and at the bottom ONE button, "Build the game", that runs the
// PrepareRunner and shows ONE progress bar in the same window (fraction, honest time left, Stop, Details); a failure is a
// plain sentence with Try again and Details; done = "Your game is ready" with Play. Then the window becomes the launcher.
#pragma once
#include <QMap>
#include <QWidget>
#include "disc_identity.h"
class QLabel;
class QLineEdit;
class QScrollArea;
namespace ps2r::launcher {
class DetailsSheet;
class FlatButton;
class FolderRow;
class GameCard;
class LauncherApp;
class PageHeader;
class ProgressPanel;
class SegmentedControl;
class SettingsRow;

class SetupPage : public QWidget {
    Q_OBJECT
public:
    enum State { Idle, Building, Failed, Done };
    SetupPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    // Opens the window: with a disc path, that disc is identified and shown; empty = the picker waits for one.
    void open(const QString& isoPath = QString());
    State state() const { return state_; }

    // the actions, as methods so the test driver and the clicks share them
    void chooseDisc(const QString& path);   // identifies `path` by content; the card or the error state follows
    void chooseFolder(const QString& folder);
    void chooseSubfolder(const QString& id, const QString& folder);   // Memory cards / Save states: Change…
    void build();
    void stop();
    void play();
    void leave();   // "Not now" / "Go to the launcher": the pages show
    bool buildEnabled(QString* why = nullptr) const;
    QString selectedTarget() const;
    void setAdvancedOpen(bool open);
    bool advancedOpen() const;
    void scrollToFolders();   // brings "Where the game lives" into view (the screens test)

    // what the test driver reads
    QString discPath() const { return disc_.path; }
    const DiscIdentity& discIdentity() const { return disc_; }
    GameCard* discCard() const { return card_; }
    QLabel* discStatus() const { return discStatus_; }
    QLineEdit* discField() const { return discField_; }
    FolderRow* folderRow() const { return folder_; }
    FolderRow* subfolderRow(const QString& id) const;   // memcards / saves (the ones the manifest asks for)
    QLabel* folderNote() const { return folderNote_; }
    SettingsRow* settingsRow(const QString& key) const;
    const QVector<SettingsRow*>& settingsRows() const { return rows_; }
    SegmentedControl* targetSelector() const { return targets_; }
    QLabel* targetNote() const { return targetNote_; }
    ProgressPanel* progress() const { return progress_; }
    FlatButton* buildButton() const { return build_; }
    FlatButton* stopButton() const { return stop_; }
    FlatButton* retryButton() const { return retry_; }
    FlatButton* detailsButton() const { return details_; }
    FlatButton* playButton() const { return play_; }
    FlatButton* leaveButton() const { return leave_; }
    FlatButton* chooseButton() const { return choose_; }
    QLabel* buildHint() const { return buildHint_; }
    QString errorText() const { return errorText_; }
    QString folderPath() const;   // the chosen folder, else the suggested one
    QMap<QString, QString> pendingSettings() const { return pending_; }

signals:
    void completed();   // the pages take over (the game plays, is ready, or the player left for now)

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* buildYourGame(QWidget* parent);
    QWidget* buildWhereItLives(QWidget* parent);
    QWidget* buildSettings(QWidget* parent);
    QWidget* buildAdvanced(QWidget* parent);
    QWidget* buildBottom(QWidget* parent);
    QLabel* sectionTitle(const QString& text, QWidget* parent);
    void setState(State state);
    void refreshDisc();
    void refreshFolder();
    void refreshSettingsRows();
    void refreshTarget();
    void refreshButtons();
    void applyLocks();
    void flushSettings();   // writes every pending row to the options file (the folder must be chosen)
    void setError(const QString& text);
    static QString etaText(qint64 seconds);

    LauncherApp* app_;
    DetailsSheet* detailsSheet_;
    PageHeader* header_;
    QScrollArea* scroll_;
    QLineEdit* discField_;
    FlatButton* choose_;
    QWidget* cardHost_;
    GameCard* card_ = nullptr;
    QLabel* discStatus_;
    FolderRow* folder_;
    QVector<FolderRow*> subfolderRows_;
    QLabel* folderNote_;
    QVector<SettingsRow*> rows_;
    FlatButton* advancedToggle_;
    QWidget* advancedBox_;
    SegmentedControl* targets_;
    QLabel* targetNote_;
    QStringList targetIds_;
    ProgressPanel* progress_;
    QLabel* error_;
    QLabel* buildHint_;
    FlatButton* build_;
    FlatButton* stop_;
    FlatButton* retry_;
    FlatButton* details_;
    FlatButton* play_;
    FlatButton* leave_;
    DiscIdentity disc_;
    QMap<QString, QString> pending_;
    QString errorText_, lastLog_;
    State state_ = Idle;
    bool dragOver_ = false;
};

}  // namespace ps2r::launcher
