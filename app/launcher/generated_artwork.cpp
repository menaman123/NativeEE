// app/launcher/generated_artwork.cpp -- the generated card artwork.
#include "generated_artwork.h"
#include <QCryptographicHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include "launcher_theme.h"
namespace ps2r::launcher {

GeneratedArtwork::GeneratedArtwork(const QString& titleName, const QString& seed, int side, QWidget* parent)
    : QWidget(parent), name_(titleName), seed_(seed), side_(side) {
    setFixedSize(side, side);
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [this] { update(); });
}

QString GeneratedArtwork::initialsOf(const QString& name) {
    QString out;
    for (const QString& w : name.split(' ', Qt::SkipEmptyParts)) {
        if (!w[0].isLetterOrNumber()) continue;
        out += w[0].toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

QPixmap GeneratedArtwork::render(const QString& titleName, const QString& seed, int side, qreal deviceRatio, bool dark) {
    LauncherTheme theme;
    theme.setDark(dark);
    QPixmap pm(int(side * deviceRatio), int(side * deviceRatio));
    pm.setDevicePixelRatio(deviceRatio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QByteArray h = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1);
    const int angle = 20 + (uchar(h[0]) % 40);            // the band's slope, from the seed
    const qreal bandPos = 0.35 + (uchar(h[1]) % 30) / 100.0;
    const int radius = LauncherTheme::radiusCard() - 2;
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), radius, radius);
    p.setClipPath(clip);
    p.fillRect(QRectF(0, 0, side, side), theme.color(LauncherTheme::AccentSoft));
    // the diagonal band
    p.save();
    p.translate(side * bandPos, side / 2.0);
    p.rotate(angle);
    p.fillRect(QRectF(-side * 0.18, -side * 1.5, side * 0.36, side * 3), theme.color(LauncherTheme::Surface));
    p.restore();
    // a thin accent line along the band's edge
    p.save();
    p.translate(side * bandPos, side / 2.0);
    p.rotate(angle);
    p.fillRect(QRectF(side * 0.18, -side * 1.5, 3, side * 3), theme.color(LauncherTheme::Accent));
    p.restore();
    QFont f = theme.font(LauncherTheme::Display);
    f.setPointSize(int(side * 0.34));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(theme.color(LauncherTheme::TextFaint));
    p.drawText(QRectF(0, 0, side, side), Qt::AlignCenter, initialsOf(titleName));
    return pm;
}

void GeneratedArtwork::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(0, 0, render(name_, seed_, side_, devicePixelRatioF(), LauncherTheme::instance().dark()));
}

}  // namespace ps2r::launcher
