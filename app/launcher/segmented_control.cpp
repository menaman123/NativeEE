// app/launcher/segmented_control.cpp -- the segmented choice, painted.
#include "segmented_control.h"
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include "launcher_theme.h"
namespace ps2r::launcher {

SegmentedControl::SegmentedControl(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFont(LauncherTheme::instance().font(LauncherTheme::Label));
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [this] { update(); });
}

void SegmentedControl::setItems(const QStringList& labels, const QStringList& values) {
    labels_ = labels;
    values_ = values;
    current_ = labels.isEmpty() ? -1 : 0;
    updateGeometry();
    update();
}

QString SegmentedControl::currentValue() const { return current_ >= 0 && current_ < values_.size() ? values_[current_] : QString(); }

void SegmentedControl::setCurrentValue(const QString& value) {
    const int i = values_.indexOf(value);
    if (i >= 0) setCurrentIndex(i, false);
}

void SegmentedControl::setCurrentIndex(int index, bool emitSignal) {
    if (index < 0 || index >= labels_.size()) return;
    const bool changed = index != current_;
    current_ = index;
    update();
    if (changed && emitSignal) emit valueChanged(values_[current_]);
}

int SegmentedControl::segmentWidth() const {
    const QFontMetrics fm(font());
    int w = 0;
    for (const QString& l : labels_) w = qMax(w, fm.horizontalAdvance(l));
    return w + 2 * LauncherTheme::S3;
}

QSize SegmentedControl::sizeHint() const {
    return QSize(segmentWidth() * qMax(1, int(labels_.size())) + 4, LauncherTheme::controlHeight() + 4);
}

int SegmentedControl::indexAt(const QPoint& pos) const {
    if (labels_.isEmpty()) return -1;
    const int w = segmentWidth();
    const int i = (pos.x() - 2) / qMax(1, w);
    return i >= 0 && i < labels_.size() ? i : -1;
}

void SegmentedControl::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int radius = LauncherTheme::radiusControl();
    const QRectF bar = QRectF(2, 2, segmentWidth() * labels_.size(), LauncherTheme::controlHeight());
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.setBrush(t.color(LauncherTheme::Surface));
    p.drawRoundedRect(bar.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.setFont(font());
    for (int i = 0; i < labels_.size(); ++i) {
        const QRectF seg(bar.left() + i * segmentWidth(), bar.top(), segmentWidth(), bar.height());
        if (i == current_ || i == hover_) {
            QPainterPath clip;
            clip.addRoundedRect(bar.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
            p.save();
            p.setClipPath(clip);
            p.setPen(Qt::NoPen);
            p.setBrush(i == current_ ? t.color(LauncherTheme::AccentSoft) : t.color(LauncherTheme::SurfaceRaised));
            p.drawRect(seg.adjusted(1, 1, -1, -1));
            p.restore();
        }
        if (i > 0) {
            p.setPen(QPen(t.color(LauncherTheme::Line), 1));
            p.drawLine(QPointF(seg.left() + 0.5, bar.top() + 6), QPointF(seg.left() + 0.5, bar.bottom() - 6));
        }
        p.setPen(i == current_ ? t.color(LauncherTheme::Accent) : (isEnabled() ? t.color(LauncherTheme::Text) : t.color(LauncherTheme::TextFaint)));
        p.drawText(seg, Qt::AlignCenter, labels_[i]);
    }
    if (hasFocus()) {
        p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(bar.adjusted(-1, -1, 1, 1), radius + 2, radius + 2);
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent* event) {
    if (!isEnabled()) return;
    const int i = indexAt(event->pos());
    if (i >= 0) setCurrentIndex(i, true);
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* event) {
    const int i = indexAt(event->pos());
    if (i != hover_) { hover_ = i; update(); }
}

void SegmentedControl::leaveEvent(QEvent*) { hover_ = -1; update(); }

void SegmentedControl::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left: setCurrentIndex(qMax(0, current_ - 1), true); return;
    case Qt::Key_Right: setCurrentIndex(qMin(int(labels_.size()) - 1, current_ + 1), true); return;
    case Qt::Key_Home: setCurrentIndex(0, true); return;
    case Qt::Key_End: setCurrentIndex(int(labels_.size()) - 1, true); return;
    default: QWidget::keyPressEvent(event);
    }
}

}  // namespace ps2r::launcher
