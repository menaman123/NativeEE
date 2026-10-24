// app/launcher/details_sheet.cpp -- the log sheet.
#include "details_sheet.h"
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include "flat_button.h"
#include "launcher_theme.h"
#include "sheet_overlay.h"
namespace ps2r::launcher {

DetailsSheet::DetailsSheet(QWidget* host) : QWidget(host) {
    hide();
    auto* content = new QWidget();
    log_ = new QPlainTextEdit(content);
    log_->setReadOnly(true);
    log_->setFont(LauncherTheme::instance().font(LauncherTheme::Mono));
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setMinimumHeight(320);
    log_->setAccessibleName(QStringLiteral("Log"));
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(log_);
    overlay_ = new SheetOverlay(QStringLiteral("Details"), content, host);
    overlay_->setPanelWidth(840);
    copy_ = new FlatButton(QStringLiteral("Copy"), FlatButton::Secondary);
    close_ = new FlatButton(QStringLiteral("Close"), FlatButton::Primary);
    overlay_->addButton(copy_);
    overlay_->addButton(close_);
    overlay_->setCancelButton(close_);
    connect(copy_, &QAbstractButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(log_->toPlainText()); });
    connect(close_, &QAbstractButton::clicked, this, [this] { overlay_->close(); });
}

void DetailsSheet::showLog(const QString& heading, const QString& text) {
    heading_ = heading;
    log_->setPlainText(text);
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    overlay_->open();
    close_->setFocus();
}

void DetailsSheet::showLogFile(const QString& heading, const QString& path) {
    QFile f(path);
    QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QStringLiteral("(no log at %1)").arg(path);
    showLog(heading, text);
}

void DetailsSheet::appendLine(const QString& line) {
    if (!overlay_->isVisible()) return;
    const bool atEnd = log_->verticalScrollBar()->value() >= log_->verticalScrollBar()->maximum() - 4;
    log_->appendPlainText(line);
    if (atEnd) log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

bool DetailsSheet::isOpen() const { return overlay_->isVisible(); }
void DetailsSheet::closeSheet() { overlay_->close(); }
QString DetailsSheet::text() const { return log_->toPlainText(); }

}  // namespace ps2r::launcher
