// app/launcher/game_card.h -- a disc's card: the generated artwork, the title, the status line, the three facts (last
// played, playtime, build), and the one action (Install / Build / Play); while a build runs the card shows the
// progress bar with elapsed / remaining, Details and Cancel.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class FlatButton;
class LauncherApp;
class ProgressPanel;

class GameCard : public QWidget {
    Q_OBJECT
public:
    GameCard(LauncherApp* app, const QString& isoPath, QWidget* parent = nullptr);
    QString isoPath() const { return iso_; }
    void refresh();
    FlatButton* actionButton() const { return action_; }
    FlatButton* detailsButton() const { return details_; }
    FlatButton* cancelButton() const { return cancel_; }
    ProgressPanel* progress() const { return progress_; }
    QString statusText() const;
    QString factsText() const;

signals:
    void actionRequested();
    void detailsRequested();
    void cancelRequested();
    void removeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void restyle();
    LauncherApp* app_;
    QString iso_;
    QLabel* title_;
    QLabel* subtitle_;
    QLabel* status_;
    QLabel* facts_;
    FlatButton* action_;
    FlatButton* details_;
    FlatButton* cancel_;
    ProgressPanel* progress_;
    QWidget* actionRow_;
    bool hover_ = false;
};

}  // namespace ps2r::launcher
