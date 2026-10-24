// app/launcher/switch_control.h -- the 40x22 pill switch: on = accent track with the knob at the right.
#pragma once
#include <QAbstractButton>
namespace ps2r::launcher {

class SwitchControl : public QAbstractButton {
    Q_OBJECT
public:
    explicit SwitchControl(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
};

}  // namespace ps2r::launcher
