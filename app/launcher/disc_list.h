// app/launcher/disc_list.h -- the discs the launcher knows, one card each (supported or badged "Not supported yet"),
// with the empty state, the error state for a file that is not the game, the drop zone, Add a disc image… and Search
// again. Shared by the first run's "Where is your game?" (selectable cards) and the Games page (cards with actions).
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
    FlatButton* rescanButton() const { return rescan_; }
    QLabel* scanNote() const { return scanNote_; }
    QString selectedDisc() const { return selected_; }
    void setSelectedDisc(const QString& isoPath);
    void chooseDisc();                          // the file picker
    void addDiscFromPath(const QString& path);  // identifies, adds or shows the error state
    void showDiscError(const QString& reason);

signals:
    void actionRequested(const QString& isoPath);
    void selectionChanged(const QString& isoPath);

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
    FlatButton* rescan_;
    QLabel* scanNote_;
    QString selected_;
    bool dragOver_ = false;
};

}  // namespace ps2r::launcher
