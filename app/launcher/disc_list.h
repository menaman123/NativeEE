// app/launcher/disc_list.h -- the discs the player has chosen, one card each (supported or badged "Not supported yet"),
// with the empty state, the error state for a file that is not the game, the drop zone and "Add a game…" (which opens
// the setup window). The launcher never searches for discs (decision D-L10). The Games page shows it with actions.
#pragma once
#include <QVector>
#include <QWidget>
class QLabel;
class QVBoxLayout;
namespace ps2r::launcher {
class EmptyState;
class FlatButton;
class GameCard;
class LauncherApp;

class DiscList : public QWidget {
    Q_OBJECT
public:
    DiscList(LauncherApp* app, bool selectable, QWidget* parent = nullptr);
    void rebuildCards();
    void refreshCards();
    GameCard* card(const QString& isoPath) const;
    const QVector<GameCard*>& cards() const { return cards_; }
    EmptyState* emptyState() const { return empty_; }
    FlatButton* addButton() const { return add_; }
    QString selectedDisc() const { return selected_; }
    void setSelectedDisc(const QString& isoPath);
    void addDiscFromPath(const QString& path);  // identifies, adds or shows the error state (a drop)
    void showDiscError(const QString& reason);

signals:
    void actionRequested(const QString& isoPath);
    void selectionChanged(const QString& isoPath);
    void addRequested();   // "Add a game…" / the empty state's button: the window opens the setup window

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    LauncherApp* app_;
    bool selectable_;
    QWidget* cardsHost_;
    QVBoxLayout* cardsLayout_;
    EmptyState* empty_;
    QVector<GameCard*> cards_;
    FlatButton* add_;
    QString selected_;
    bool dragOver_ = false;
};

}  // namespace ps2r::launcher
