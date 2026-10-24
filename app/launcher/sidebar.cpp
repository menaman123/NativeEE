// app/launcher/sidebar.cpp -- the sidebar, painted.
#include "sidebar.h"
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include "app_data.h"
#include "flat_button.h"
#include "launcher_theme.h"
namespace ps2r::launcher {

namespace {
constexpr int kItemHeight = 36;
constexpr int kItemsTop = 104;
}  // namespace

Sidebar::Sidebar(const QString& product, const QString& version, const QStringList& pages, QWidget* parent)
    : QWidget(parent), product_(product), version_(version), pages_(pages) {
    setFixedWidth(LauncherTheme::sidebarWidth());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Pages"));
    open_ = new FlatButton(QStringLiteral("Open"), FlatButton::Quiet, this);
    open_->setAccessibleName(QStringLiteral("Open the app-data folder"));
    connect(open_, &QAbstractButton::clicked, this, &Sidebar::openRequested);
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [this] { update(); });
}

void Sidebar::setCurrent(int index) {
    if (index < 0 || index >= pages_.size()) return;
    current_ = index;
    update();
}

void Sidebar::setFooter(const QString& path) { footer_ = path; update(); }

QRect Sidebar::itemRect(int index) const {
    return QRect(LauncherTheme::S2, kItemsTop + index * (kItemHeight + LauncherTheme::S1), width() - 2 * LauncherTheme::S2, kItemHeight);
}

int Sidebar::itemAt(const QPoint& pos) const {
    for (int i = 0; i < pages_.size(); ++i) if (itemRect(i).contains(pos)) return i;
    return -1;
}

void Sidebar::resizeEvent(QResizeEvent*) {
    open_->move(LauncherTheme::S3 - 4, height() - LauncherTheme::S3 - open_->sizeHint().height());
    open_->resize(open_->sizeHint());
}

void Sidebar::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), t.color(LauncherTheme::Sidebar));
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.drawLine(width() - 1, 0, width() - 1, height());
    // the product name and version
    p.setPen(t.color(LauncherTheme::Text));
    p.setFont(t.font(LauncherTheme::Title));
    p.drawText(QRect(LauncherTheme::S3, LauncherTheme::S4 + 4, width() - 2 * LauncherTheme::S3, 24), Qt::AlignLeft | Qt::AlignVCenter, product_);
    p.setPen(t.color(LauncherTheme::TextMuted));
    p.setFont(t.font(LauncherTheme::Caption));
    p.drawText(QRect(LauncherTheme::S3, LauncherTheme::S4 + 30, width() - 2 * LauncherTheme::S3, 16), Qt::AlignLeft | Qt::AlignVCenter, version_);
    // the items
    p.setFont(t.font(LauncherTheme::Label));
    for (int i = 0; i < pages_.size(); ++i) {
        const QRect r = itemRect(i);
        if (i == current_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.color(LauncherTheme::AccentSoft));
            p.drawRoundedRect(r, LauncherTheme::radiusControl(), LauncherTheme::radiusControl());
        } else if (i == hover_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.color(LauncherTheme::SurfaceRaised));
            p.drawRoundedRect(r, LauncherTheme::radiusControl(), LauncherTheme::radiusControl());
        }
        p.setPen(i == current_ ? t.color(LauncherTheme::Accent) : t.color(LauncherTheme::Text));
        p.drawText(r.adjusted(LauncherTheme::S3 - LauncherTheme::S2, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, pages_[i]);
        if (hasFocus() && i == current_) {
            p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(-1, -1, 1, 1), LauncherTheme::radiusControl() + 2, LauncherTheme::radiusControl() + 2);
        }
    }
    // the footer
    p.setPen(t.color(LauncherTheme::TextMuted));
    p.setFont(t.font(LauncherTheme::Caption));
    const QFontMetrics fm(p.font());
    const QRect footer(LauncherTheme::S3, height() - LauncherTheme::S3 - open_->height() - 18, width() - 2 * LauncherTheme::S3, 16);
    p.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(QStringLiteral("app data: %1").arg(AppData::shortPath(footer_)), Qt::ElideMiddle, footer.width()));
}

void Sidebar::mousePressEvent(QMouseEvent* event) {
    const int i = itemAt(event->pos());
    if (i >= 0 && i != current_) { setCurrent(i); emit selected(i); }
}

void Sidebar::mouseMoveEvent(QMouseEvent* event) {
    const int i = itemAt(event->pos());
    if (i != hover_) { hover_ = i; update(); }
}

void Sidebar::leaveEvent(QEvent*) { hover_ = -1; update(); }

void Sidebar::keyPressEvent(QKeyEvent* event) {
    int next = current_;
    if (event->key() == Qt::Key_Up) next = qMax(0, current_ - 1);
    else if (event->key() == Qt::Key_Down) next = qMin(int(pages_.size()) - 1, current_ + 1);
    else { QWidget::keyPressEvent(event); return; }
    if (next != current_) { setCurrent(next); emit selected(next); }
}

}  // namespace ps2r::launcher
