// app/launcher/game_card.h -- a disc's card: the generated artwork, the title, the region line, the "Not supported
// yet" badge with its plain reason when the disc is recognised but not playable, the status line, the facts (last
// played, playtime, prepared when) and the one action (Prepare / Play). In the first run the card is selectable
// instead (a click picks the disc) and carries no action button.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class FlatButton;
class LauncherApp;

class GameCard : public QWidget {
    Q_OBJECT
public:
    GameCard(LauncherApp* app, const QString& isoPath, QWidget* parent = nullptr);
    QString isoPath() const { return iso_; }
    void refresh();
    void setSelectable(bool on);
    void setSelected(bool on);
    bool selected() const { return selected_; }
    bool supported() const;
    FlatButton* actionButton() const { return action_; }
    QString statusText() const;
    QString factsText() const;
    QString badgeText() const;

signals:
    void actionRequested();
    void selectRequested();
    void removeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void restyle();
    LauncherApp* app_;
    QString iso_;
    QLabel* title_;
    QLabel* subtitle_;
    QLabel* badge_;
    QLabel* status_;
    QLabel* facts_;
    FlatButton* action_;
    QWidget* actionRow_;
    bool hover_ = false, selectable_ = false, selected_ = false;
};

}  // namespace ps2r::launcher
