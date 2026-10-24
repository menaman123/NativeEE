// app/launcher/folder_row.cpp -- the folder setting row.
#include "folder_row.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QUrl>
#include "app_data.h"
#include "flat_button.h"
#include "launcher_theme.h"
namespace ps2r::launcher {

FolderRow::FolderRow(const QString& label, QWidget* parent) : QWidget(parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    setMinimumHeight(LauncherTheme::rowHeight());
    label_ = new QLabel(label, this);
    label_->setFont(t.font(LauncherTheme::Label));
    label_->setMinimumWidth(qMax(128, QFontMetrics(label_->font()).horizontalAdvance(label) + LauncherTheme::S3));
    pathLabel_ = new QLabel(this);
    pathLabel_->setFont(t.font(LauncherTheme::Body));
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    change_ = new FlatButton(QStringLiteral("Change…"), FlatButton::Secondary, this);
    open_ = new FlatButton(openInFileManagerLabel(), FlatButton::Quiet, this);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, LauncherTheme::S1, 0, LauncherTheme::S1);
    row->setSpacing(LauncherTheme::S2);
    row->addWidget(label_);
    row->addWidget(pathLabel_, 1);
    row->addWidget(change_);
    row->addWidget(open_);
    connect(change_, &QAbstractButton::clicked, this, &FolderRow::changeRequested);
    connect(open_, &QAbstractButton::clicked, this, [this] { revealInFileManager(path_); });
    connect(&t, &LauncherTheme::changed, this, [this] { relayout(); });
    relayout();
}

QString FolderRow::openInFileManagerLabel() {
#ifdef Q_OS_MACOS
    return QStringLiteral("Open in Finder");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Open in Explorer");
#else
    return QStringLiteral("Open folder");
#endif
}

void FolderRow::revealInFileManager(const QString& path) {
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void FolderRow::setPath(const QString& path, const QString& shownAs) {
    path_ = path;
    shownAs_ = shownAs;
    relayout();
}

void FolderRow::relayout() {
    const LauncherTheme& t = LauncherTheme::instance();
    label_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    pathLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    const QFontMetrics fm(pathLabel_->font());
    pathLabel_->setText(fm.elidedText(shownAs_.isEmpty() ? AppData::shortPath(path_) : shownAs_, Qt::ElideMiddle, qMax(80, pathLabel_->width() - 8)));
    pathLabel_->setToolTip(path_);
    pathLabel_->setAccessibleName(path_);
}

void FolderRow::resizeEvent(QResizeEvent*) { relayout(); }

void FolderRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setPen(QPen(LauncherTheme::instance().color(LauncherTheme::Line), 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

}  // namespace ps2r::launcher
