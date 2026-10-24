// app/launcher/flat_button.h -- the design's button: primary (accent fill), secondary (outline) or quiet (text),
// painted by the launcher, with the 2 px focus ring. Height 32, or 40 for the one thing to do on a page.
#pragma once
#include <QAbstractButton>
namespace ps2r::launcher {

class FlatButton : public QAbstractButton {
    Q_OBJECT
public:
    enum Kind { Primary, Secondary, Quiet };
    explicit FlatButton(const QString& text, Kind kind = Secondary, QWidget* parent = nullptr);
    void setKind(Kind kind);
    Kind kind() const { return kind_; }
    void setLarge(bool large);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    Kind kind_;
    bool hover_ = false, large_ = false;
};

}  // namespace ps2r::launcher
