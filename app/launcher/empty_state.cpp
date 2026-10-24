// app/launcher/empty_state.cpp -- the empty / error state block.
#include "empty_state.h"
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include "flat_button.h"
#include "launcher_theme.h"
namespace ps2r::launcher {

EmptyState::EmptyState(const QString& title, const QString& body, const QString& primaryText, const QString& quietText, QWidget* parent)
    : QWidget(parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    glyph_ = new QWidget(this);
    glyph_->setFixedSize(64, 64);
    title_ = new QLabel(title, this);
    title_->setFont(t.font(LauncherTheme::Title));
    title_->setAlignment(Qt::AlignCenter);
    title_->setWordWrap(true);
    body_ = new QLabel(body, this);
    body_->setFont(t.font(LauncherTheme::Body));
    body_->setAlignment(Qt::AlignCenter);
    body_->setWordWrap(true);
    primary_ = new FlatButton(primaryText, FlatButton::Primary, this);
    primary_->setLarge(true);
    quiet_ = new FlatButton(quietText, FlatButton::Quiet, this);
    quiet_->setVisible(!quietText.isEmpty());
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(LauncherTheme::S6 * 2, LauncherTheme::S6, LauncherTheme::S6 * 2, LauncherTheme::S6);
    col->setSpacing(LauncherTheme::S3);
    col->addStretch(1);
    col->addWidget(glyph_, 0, Qt::AlignHCenter);
    // the wrapped labels take the full width (their text is centred): a label placed with an alignment flag is not
    // given its wrapped height, and its lines run into the next widget
    col->addWidget(title_);
    col->addWidget(body_);
    col->addSpacing(LauncherTheme::S2);
    col->addWidget(primary_, 0, Qt::AlignHCenter);
    col->addWidget(quiet_, 0, Qt::AlignHCenter);
    col->addStretch(1);
    connect(primary_, &QAbstractButton::clicked, this, &EmptyState::primaryClicked);
    connect(quiet_, &QAbstractButton::clicked, this, &EmptyState::quietClicked);
    connect(&t, &LauncherTheme::changed, this, &EmptyState::restyle);
    restyle();
}

void EmptyState::setTexts(const QString& title, const QString& body) {
    title_->setText(title);
    body_->setText(body);
}

void EmptyState::setTone(Tone tone) { tone_ = tone; update(); }

void EmptyState::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    body_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

void EmptyState::paintEvent(QPaintEvent*) {
    // the disc glyph: two rings, painted in the glyph widget's rectangle
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF g = QRectF(glyph_->geometry());
    const QColor c = tone_ == Error ? t.color(LauncherTheme::Error) : t.color(LauncherTheme::TextFaint);
    p.setPen(QPen(c, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(g.adjusted(2, 2, -2, -2));
    p.drawEllipse(g.center(), 10.0, 10.0);
    p.setPen(QPen(c, 1));
    p.drawEllipse(g.adjusted(14, 14, -14, -14));
}

}  // namespace ps2r::launcher
