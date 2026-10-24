// app/launcher/page_header.cpp -- the page title block.
#include "page_header.h"
#include <QLabel>
#include <QVBoxLayout>
#include "launcher_theme.h"
namespace ps2r::launcher {

PageHeader::PageHeader(const QString& title, const QString& description, QWidget* parent) : QWidget(parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    title_ = new QLabel(title, this);
    title_->setFont(t.font(LauncherTheme::Display));
    title_->setAccessibleName(title);
    description_ = new QLabel(description, this);
    description_->setFont(t.font(LauncherTheme::Body));
    description_->setWordWrap(true);
    description_->setMaximumWidth(560);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, LauncherTheme::S6 - LauncherTheme::S5, 0, LauncherTheme::S4);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(title_);
    col->addWidget(description_);
    description_->setVisible(!description.isEmpty());
    connect(&t, &LauncherTheme::changed, this, &PageHeader::restyle);
    restyle();
}

void PageHeader::setDescription(const QString& description) {
    description_->setText(description);
    description_->setVisible(!description.isEmpty());
}

void PageHeader::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    description_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

}  // namespace ps2r::launcher
