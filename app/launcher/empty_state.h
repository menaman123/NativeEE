// app/launcher/empty_state.h -- the centred empty / error state: a painted 64 px glyph (a disc), a title sentence, a
// body sentence, one primary button and an optional quiet one.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class FlatButton;

class EmptyState : public QWidget {
    Q_OBJECT
public:
    enum Tone { Neutral, Error };
    EmptyState(const QString& title, const QString& body, const QString& primaryText, const QString& quietText,
               QWidget* parent = nullptr);
    void setTexts(const QString& title, const QString& body);
    void setTone(Tone tone);
    FlatButton* primaryButton() const { return primary_; }
    FlatButton* quietButton() const { return quiet_; }

signals:
    void primaryClicked();
    void quietClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void restyle();
    QLabel* title_;
    QLabel* body_;
    FlatButton* primary_;
    FlatButton* quiet_;
    QWidget* glyph_;
    Tone tone_ = Neutral;
};

}  // namespace ps2r::launcher
