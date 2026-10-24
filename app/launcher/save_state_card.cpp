// app/launcher/save_state_card.cpp -- the save-state card.
#include "save_state_card.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>
#include "flat_button.h"
#include "launcher_theme.h"
namespace ps2r::launcher {

namespace {
constexpr int kThumbW = 128, kThumbH = 96;

QString sizeText(qint64 bytes) {
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
}
}  // namespace

QPixmap SaveStateCard::neutralTile(const SaveStateEntry& entry, const QSize& size, qreal deviceRatio, bool dark) {
    QPixmap pm(size * deviceRatio);
    pm.setDevicePixelRatio(deviceRatio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(0, 0, size.width(), size.height());
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, dark ? QColor("#2A2D34") : QColor("#E3E6EC"));
    g.setColorAt(1, dark ? QColor("#1B1C21") : QColor("#CFD3DB"));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(r, LauncherTheme::radiusControl(), LauncherTheme::radiusControl());
    // a film-frame mark: two short bars, the frame count beneath
    p.setBrush(dark ? QColor("#3A3D46") : QColor("#B8BDC7"));
    p.drawRoundedRect(QRectF(r.center().x() - 22, r.center().y() - 18, 44, 6), 3, 3);
    p.drawRoundedRect(QRectF(r.center().x() - 22, r.center().y() - 6, 44, 6), 3, 3);
    p.setPen(dark ? QColor("#A1A6B2") : QColor("#5E6470"));
    p.setFont(LauncherTheme::instance().font(LauncherTheme::Caption));
    p.drawText(r.adjusted(0, 0, 0, -6), Qt::AlignHCenter | Qt::AlignBottom, entry.frame > 0 ? QStringLiteral("frame %1").arg(entry.frame) : QStringLiteral("no picture"));
    return pm;
}

SaveStateCard::SaveStateCard(const SaveStateEntry& entry, QWidget* parent) : QWidget(parent), entry_(entry) {
    const LauncherTheme& t = LauncherTheme::instance();
    setMinimumHeight(kThumbH + 2 * LauncherTheme::S3);
    setAccessibleName(QStringLiteral("Save state %1").arg(entry.name));
    thumb_ = new QLabel(this);
    thumb_->setFixedSize(kThumbW, kThumbH);
    QPixmap pm;
    if (!entry.thumbnailPath.isEmpty()) pm.load(entry.thumbnailPath);
    if (pm.isNull()) pm = neutralTile(entry, QSize(kThumbW, kThumbH), devicePixelRatioF(), t.dark());
    else pm = pm.scaled(QSize(kThumbW, kThumbH) * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(devicePixelRatioF());
    thumb_->setPixmap(pm);
    thumb_->setAlignment(Qt::AlignCenter);
    name_ = new QLabel(entry.name, this);
    name_->setFont(t.font(LauncherTheme::Title));
    facts_ = new QLabel(this);
    facts_->setFont(t.font(LauncherTheme::Body));
    facts_->setWordWrap(true);
    QStringList facts;
    if (entry.when.isValid()) facts << entry.when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    if (entry.frame > 0) facts << QStringLiteral("frame %1").arg(entry.frame);
    facts << sizeText(entry.sizeBytes);
    facts_->setText(facts.join(QStringLiteral(" · ")));
    resume_ = new FlatButton(QStringLiteral("Resume"), FlatButton::Primary, this);
    resume_->setAccessibleName(QStringLiteral("Resume %1").arg(entry.name));
    auto* text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(LauncherTheme::S1);
    text->addWidget(name_);
    text->addWidget(facts_);
    text->addStretch(1);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3);
    row->setSpacing(LauncherTheme::S3);
    row->addWidget(thumb_, 0, Qt::AlignTop);
    row->addLayout(text, 1);
    row->addWidget(resume_, 0, Qt::AlignVCenter);
    connect(resume_, &QAbstractButton::clicked, this, &SaveStateCard::resumeRequested);
    connect(&t, &LauncherTheme::changed, this, [this] { restyle(); update(); });
    restyle();
}

QString SaveStateCard::factsText() const { return facts_->text(); }

void SaveStateCard::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    name_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    facts_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

void SaveStateCard::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.setBrush(t.color(LauncherTheme::Surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
}

}  // namespace ps2r::launcher
