// app/launcher/sheet_overlay.cpp -- the sheet, painted over the page.
#include "sheet_overlay.h"
#include <QAbstractButton>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include "launcher_theme.h"
namespace ps2r::launcher {

SheetOverlay::SheetOverlay(const QString& heading, QWidget* content, QWidget* parent) : QWidget(parent), content_(content) {
    const LauncherTheme& t = LauncherTheme::instance();
    setFocusPolicy(Qt::StrongFocus);
    panel_ = new QWidget(this);
    panel_->setAutoFillBackground(false);
    heading_ = new QLabel(heading, panel_);
    heading_->setFont(t.font(LauncherTheme::Title));
    heading_->setAccessibleName(heading);
    content_->setParent(panel_);
    auto* col = new QVBoxLayout(panel_);
    col->setContentsMargins(LauncherTheme::S4, LauncherTheme::S4, LauncherTheme::S4, LauncherTheme::S4);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(heading_);
    col->addWidget(content_, 1);
    buttons_ = new QHBoxLayout();
    buttons_->setSpacing(LauncherTheme::S2);
    buttons_->addStretch(1);
    col->addLayout(buttons_);
    parent->installEventFilter(this);
    hide();
    connect(&t, &LauncherTheme::changed, this, [this] { heading_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::Text))); update(); });
    heading_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
}

void SheetOverlay::addButton(QWidget* button) { buttons_->addWidget(button); }
void SheetOverlay::setPanelWidth(int width) { panelWidth_ = width; place(); }

void SheetOverlay::open() {
    if (parentWidget()) resize(parentWidget()->size());
    place();
    show();
    raise();
    setFocus();
}

void SheetOverlay::close() {
    hide();
    emit closed();
}

void SheetOverlay::place() {
    const int w = qMin(panelWidth_, width() - 2 * LauncherTheme::S5);
    const int h = qMin(panel_->sizeHint().height(), height() - 2 * LauncherTheme::S5);
    panel_->setGeometry((width() - w) / 2, qMax(int(LauncherTheme::S5), (height() - h) / 2), w, h);
}

void SheetOverlay::resizeEvent(QResizeEvent*) { place(); }

bool SheetOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        resize(parentWidget()->size());
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void SheetOverlay::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor dim = t.dark() ? QColor(0, 0, 0) : QColor(20, 22, 26);
    dim.setAlphaF(0.4f);
    p.fillRect(rect(), dim);
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.setBrush(t.color(LauncherTheme::Surface));
    p.drawRoundedRect(QRectF(panel_->geometry()).adjusted(0.5, 0.5, -0.5, -0.5), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
}

void SheetOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (cancel_ && cancel_->isEnabled()) cancel_->click();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SheetOverlay::mousePressEvent(QMouseEvent*) {}   // the dimmed page does not react

}  // namespace ps2r::launcher
