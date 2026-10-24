// app/launcher/switch_control.cpp -- the pill switch, painted.
#include "switch_control.h"
#include <QKeyEvent>
#include <QPainter>
#include "launcher_theme.h"
namespace ps2r::launcher {

SwitchControl::SwitchControl(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [this] { update(); });
}

QSize SwitchControl::sizeHint() const { return QSize(44, 26); }

void SwitchControl::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(2, 2, 40, 22);
    QColor trackColor = isChecked() ? t.color(LauncherTheme::Accent) : t.color(LauncherTheme::Line);
    if (!isEnabled()) trackColor.setAlphaF(0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, 11, 11);
    const qreal knobX = isChecked() ? track.right() - 20 : track.left() + 2;
    p.setBrush(isChecked() ? t.color(LauncherTheme::AccentText) : t.color(LauncherTheme::Surface));
    p.drawEllipse(QRectF(knobX, track.top() + 2, 18, 18));
    if (hasFocus()) {
        p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 13, 13);
    }
}

void SwitchControl::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) { click(); return; }
    QAbstractButton::keyPressEvent(event);
}

}  // namespace ps2r::launcher
