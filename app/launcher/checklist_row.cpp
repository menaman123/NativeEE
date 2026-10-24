// app/launcher/checklist_row.cpp -- the checklist step, painted.
#include "checklist_row.h"
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include "flat_button.h"
#include "launcher_theme.h"
namespace ps2r::launcher {

namespace {
constexpr int kGlyph = 16;
constexpr int kGlyphColumn = 32;
}  // namespace

ChecklistRow::ChecklistRow(const InstallStep& step, QWidget* parent) : QWidget(parent), step_(step) {
    const LauncherTheme& t = LauncherTheme::instance();
    name_ = new QLabel(step.name, this);
    name_->setFont(t.font(LauncherTheme::Label));
    name_->setMinimumWidth(qMax(160, QFontMetrics(name_->font()).horizontalAdvance(QStringLiteral("PlayStation Vita tools")) + LauncherTheme::S3));
    detail_ = new QLabel(this);
    detail_->setFont(t.font(LauncherTheme::Body));
    detail_->setMinimumWidth(80);
    detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // a long detail is elided, never widens the row
    size_ = new QLabel(this);
    size_->setFont(t.font(LauncherTheme::Caption));
    size_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    size_->setMinimumWidth(QFontMetrics(size_->font()).horizontalAdvance(QStringLiteral("9999 MB")) + LauncherTheme::S2);
    reason_ = new QLabel(this);
    reason_->setFont(t.font(LauncherTheme::Body));
    reason_->setWordWrap(true);
    retry_ = new FlatButton(QStringLiteral("Retry"), FlatButton::Quiet, this);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet, this);
    install_ = new FlatButton(QStringLiteral("Install"), FlatButton::Primary, this);
    install_->setAccessibleName(QStringLiteral("Install %1").arg(step.name));
    retry_->setAccessibleName(QStringLiteral("Retry %1").arg(step.name));
    auto* top = new QHBoxLayout();
    top->setContentsMargins(kGlyphColumn, 0, 0, 0);
    top->setSpacing(LauncherTheme::S2);
    top->addWidget(name_);
    top->addWidget(detail_, 1);
    top->addWidget(size_);
    reasonRow_ = new QWidget(this);
    auto* bottom = new QHBoxLayout(reasonRow_);
    bottom->setContentsMargins(kGlyphColumn, 0, 0, 0);
    bottom->setSpacing(LauncherTheme::S2);
    bottom->addWidget(reason_, 1);
    bottom->addWidget(install_);
    bottom->addWidget(retry_);
    bottom->addWidget(details_);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, LauncherTheme::S2, 0, LauncherTheme::S2);
    col->setSpacing(LauncherTheme::S1);
    col->addLayout(top);
    col->addWidget(reasonRow_);
    pulse_ = new QTimer(this);
    pulse_->setInterval(400);
    connect(pulse_, &QTimer::timeout, this, [this] { phase_ = (phase_ + 1) % 2; update(); });
    connect(retry_, &QAbstractButton::clicked, this, &ChecklistRow::retryRequested);
    connect(details_, &QAbstractButton::clicked, this, &ChecklistRow::detailsRequested);
    connect(install_, &QAbstractButton::clicked, this, &ChecklistRow::installRequested);
    connect(&t, &LauncherTheme::changed, this, &ChecklistRow::restyle);
    setStep(step);
}

void ChecklistRow::setStep(const InstallStep& step) {
    step_ = step;
    elideDetail();
    size_->setText(Installer::bytesText(step.bytes));
    reason_->setText(step.reason);
    const bool failed = step.state == InstallStep::Failed;
    reasonRow_->setVisible(failed);
    install_->setVisible(failed && step.canPromptInstall);
    if (step.state == InstallStep::Running) pulse_->start(); else pulse_->stop();
    restyle();
    update();
}

void ChecklistRow::elideDetail() {
    const QFontMetrics fm(detail_->font());
    detail_->setText(fm.elidedText(step_.detail, Qt::ElideRight, qMax(40, detail_->width())));
    detail_->setToolTip(step_.detail);
}

void ChecklistRow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    elideDetail();
}

void ChecklistRow::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    name_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(step_.state == InstallStep::Skipped ? LauncherTheme::TextFaint : LauncherTheme::Text)));
    detail_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    size_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    reason_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Error)));
}

void ChecklistRow::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF g(LauncherTheme::S2, LauncherTheme::S2 + (name_->height() - kGlyph) / 2.0, kGlyph, kGlyph);
    switch (step_.state) {
    case InstallStep::Pending:
        p.setPen(QPen(t.color(LauncherTheme::Line), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(g.adjusted(1, 1, -1, -1));
        break;
    case InstallStep::Running:
        p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(g.adjusted(1, 1, -1, -1));
        p.setPen(Qt::NoPen);
        p.setBrush(t.color(LauncherTheme::Accent));
        p.drawEllipse(g.center(), phase_ ? 3.0 : 4.5, phase_ ? 3.0 : 4.5);
        break;
    case InstallStep::Done:
        p.setPen(Qt::NoPen);
        p.setBrush(t.color(LauncherTheme::Ok));
        p.drawEllipse(g);
        p.setPen(QPen(t.color(LauncherTheme::Surface), 2));
        p.drawPolyline(QPolygonF({g.topLeft() + QPointF(4, 8.5), g.topLeft() + QPointF(7, 11.5), g.topLeft() + QPointF(12.5, 5)}));
        break;
    case InstallStep::Failed:
        p.setPen(Qt::NoPen);
        p.setBrush(t.color(LauncherTheme::Error));
        p.drawEllipse(g);
        p.setPen(QPen(t.color(LauncherTheme::Surface), 2));
        p.drawLine(g.topLeft() + QPointF(5, 5), g.topLeft() + QPointF(11, 11));
        p.drawLine(g.topLeft() + QPointF(11, 5), g.topLeft() + QPointF(5, 11));
        break;
    case InstallStep::Skipped:
        p.setPen(QPen(t.color(LauncherTheme::TextFaint), 2));
        p.drawLine(g.left() + 3, g.center().y(), g.right() - 3, g.center().y());
        break;
    }
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.drawLine(kGlyphColumn, height() - 1, width(), height() - 1);
}

}  // namespace ps2r::launcher
