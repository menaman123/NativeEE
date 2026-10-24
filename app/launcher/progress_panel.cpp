// app/launcher/progress_panel.cpp -- the progress bar block.
#include "progress_panel.h"
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include "launcher_theme.h"
namespace ps2r::launcher {

ProgressPanel::ProgressPanel(QWidget* parent) : QWidget(parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    text_ = new QLabel(this);
    text_->setFont(t.font(LauncherTheme::Body));
    bar_ = new QWidget(this);
    bar_->setFixedHeight(4);
    caption_ = new QLabel(this);
    caption_->setFont(t.font(LauncherTheme::Caption));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(text_);
    col->addWidget(bar_);
    col->addWidget(caption_);
    sweep_ = new QTimer(this);
    sweep_->setInterval(30);
    connect(sweep_, &QTimer::timeout, this, [this] { sweepPos_ = (sweepPos_ + 4) % 1000; update(); });
    connect(&t, &LauncherTheme::changed, this, &ProgressPanel::restyle);
    restyle();
}

void ProgressPanel::setText(const QString& text) { text_->setText(text); text_->setAccessibleName(text); }
void ProgressPanel::setCaption(const QString& caption) { caption_->setText(caption); }
QString ProgressPanel::text() const { return text_->text(); }
QString ProgressPanel::caption() const { return caption_->text(); }

void ProgressPanel::setProgress(int done, int total) {
    done_ = done;
    total_ = total;
    setIndeterminate(total <= 0);
    update();
}

void ProgressPanel::setIndeterminate(bool on) {
    indeterminate_ = on;
    if (on && isVisible()) sweep_->start(); else sweep_->stop();
    update();
}

void ProgressPanel::clear() {
    text_->clear();
    caption_->clear();
    done_ = total_ = 0;
    setIndeterminate(false);
}

void ProgressPanel::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    text_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    caption_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

void ProgressPanel::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(bar_->geometry());
    p.setPen(Qt::NoPen);
    p.setBrush(t.color(LauncherTheme::Line));
    p.drawRoundedRect(r, 2, 2);
    p.setBrush(t.color(LauncherTheme::Accent));
    if (indeterminate_) {
        if (!sweep_->isActive()) sweep_->start();
        const qreal w = r.width() * 0.25;
        const qreal x = r.left() + (r.width() + w) * (sweepPos_ / 1000.0) - w;
        p.drawRoundedRect(QRectF(qMax(r.left(), x), r.top(), qMin(w, r.right() - qMax(r.left(), x)), r.height()), 2, 2);
    } else if (total_ > 0) {
        p.drawRoundedRect(QRectF(r.left(), r.top(), r.width() * qBound(0.0, double(done_) / total_, 1.0), r.height()), 2, 2);
    }
}

}  // namespace ps2r::launcher
