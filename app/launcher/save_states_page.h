// app/launcher/save_states_page.h -- Save States: the states in the game folder's saves, newest first, each with
// its thumbnail (or a neutral tile), when it was saved and Resume; the empty state when there are none or no game
// folder yet; Open the folder and Refresh.
#pragma once
#include <QVector>
#include <QWidget>
class QLabel;
class QVBoxLayout;
namespace ps2r::launcher {
class EmptyState;
class FlatButton;
class LauncherApp;
class SaveStateCard;
class SaveStateList;

class SaveStatesPage : public QWidget {
    Q_OBJECT
public:
    explicit SaveStatesPage(LauncherApp* app, QWidget* parent = nullptr);
    ~SaveStatesPage() override;
    void refresh();
    const QVector<SaveStateCard*>& cards() const { return cards_; }
    EmptyState* emptyState() const { return empty_; }
    FlatButton* openButton() const { return open_; }
    FlatButton* refreshButton() const { return refresh_; }
    void resume(const QString& statePath);   // the card's button, as a method for the driver
    QString errorText() const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    LauncherApp* app_;
    SaveStateList* list_;
    QWidget* cardsHost_;
    QVBoxLayout* cardsLayout_;
    QVector<SaveStateCard*> cards_;
    EmptyState* empty_;
    QLabel* note_;
    QLabel* error_;
    FlatButton* open_;
    FlatButton* refresh_;
};

}  // namespace ps2r::launcher
