// app/launcher/flat_button.cpp -- the three button kinds, painted.
#include "flat_button.h"
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include "launcher_theme.h"
namespace ps2r::launcher {

FlatButton::FlatButton(const QString& text, Kind kind, QWidget* parent) : QAbstractButton(parent), kind_(kind) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFont(LauncherTheme::instance().font(LauncherTheme::Label));
    setAccessibleName(text);
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [this] { update(); });
}

void FlatButton::setKind(Kind kind) { kind_ = kind; update(); }
void FlatButton::setLarge(bool large) { large_ = large; updateGeometry(); update(); }

QSize FlatButton::sizeHint() const {
    const QFontMetrics fm(font());
    const int h = large_ ? LauncherTheme::largeControlHeight() : LauncherTheme::controlHeight();
    const int pad = kind_ == Quiet ? LauncherTheme::S2 : (large_ ? LauncherTheme::S4 : LauncherTheme::S3);
    return QSize(fm.horizontalAdvance(text()) + 2 * pad + 4, h + 4);   // +4: room for the focus ring
}

void FlatButton::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const int radius = LauncherTheme::radiusControl();
    QColor fill, border, fg;
    switch (kind_) {
    case Primary:
        fill = isEnabled() ? t.color(LauncherTheme::Accent) : t.color(LauncherTheme::Surface);
        if (isEnabled() && (hover_ || isDown())) fill = fill.lighter(isDown() ? 90 : 108);
        border = isEnabled() ? fill : t.color(LauncherTheme::Line);
        fg = isEnabled() ? t.color(LauncherTheme::AccentText) : t.color(LauncherTheme::TextFaint);
        break;
    case Secondary:
        fill = (hover_ && isEnabled()) ? t.color(LauncherTheme::SurfaceRaised) : t.color(LauncherTheme::Surface);
        if (isDown()) fill = t.color(LauncherTheme::AccentSoft);
        border = t.color(LauncherTheme::Line);
        fg = isEnabled() ? t.color(LauncherTheme::Text) : t.color(LauncherTheme::TextFaint);
        break;
    case Quiet:
    default:
        fill = (hover_ && isEnabled()) ? t.color(LauncherTheme::AccentSoft) : Qt::transparent;
        border = Qt::transparent;
        fg = isEnabled() ? t.color(LauncherTheme::Accent) : t.color(LauncherTheme::TextFaint);
        break;
    }
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    if (hasFocus()) {
        p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius + 2, radius + 2);
    }
    p.setPen(fg);
    p.setFont(font());
    p.drawText(r, Qt::AlignCenter, text());
}

void FlatButton::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void FlatButton::leaveEvent(QEvent*) { hover_ = false; update(); }

void FlatButton::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { click(); return; }
    QAbstractButton::keyPressEvent(event);
}

}  // namespace ps2r::launcher
