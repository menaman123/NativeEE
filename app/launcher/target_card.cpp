// app/launcher/target_card.cpp -- the target card.
#include "target_card.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSysInfo>
#include <QVBoxLayout>
#include "app_data.h"
#include "build_runner.h"
#include "flat_button.h"
#include "folder_row.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "progress_panel.h"
namespace ps2r::launcher {

TargetCard::TargetCard(LauncherApp* app, const TargetSpec& spec, QWidget* parent) : QWidget(parent), app_(app), spec_(spec) {
    const LauncherTheme& t = LauncherTheme::instance();
    setObjectName(QStringLiteral("target_%1").arg(spec.id));
    title_ = new QLabel(this);
    title_->setFont(t.font(LauncherTheme::Title));
    status_ = new QLabel(this);
    status_->setFont(t.font(LauncherTheme::Body));
    status_->setWordWrap(true);
    path_ = new QLabel(this);
    path_->setFont(t.font(LauncherTheme::Caption));
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    howTo_ = new QLabel(spec.installText, this);
    howTo_->setFont(t.font(LauncherTheme::Body));
    howTo_->setWordWrap(true);
    howTo_->setVisible(!spec.installText.isEmpty());
    build_ = new FlatButton(QStringLiteral("Build"), FlatButton::Primary, this);
    show_ = new FlatButton(FolderRow::openInFileManagerLabel().replace(QStringLiteral("Open in"), QStringLiteral("Show in")), FlatButton::Quiet, this);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet, this);
    cancel_ = new FlatButton(QStringLiteral("Cancel"), FlatButton::Quiet, this);
    progress_ = new ProgressPanel(this);
    progress_->hide();
    auto* actions = new QHBoxLayout();
    actions->setSpacing(LauncherTheme::S2);
    actions->addStretch(1);
    actions->addWidget(details_);
    actions->addWidget(show_);
    actions->addWidget(cancel_);
    actions->addWidget(build_);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(title_);
    col->addWidget(status_);
    col->addWidget(path_);
    col->addWidget(howTo_);
    col->addWidget(progress_);
    col->addLayout(actions);
    connect(build_, &QAbstractButton::clicked, this, &TargetCard::buildRequested);
    connect(details_, &QAbstractButton::clicked, this, &TargetCard::detailsRequested);
    connect(cancel_, &QAbstractButton::clicked, this, [this] { app_->builder()->cancel(); });
    connect(show_, &QAbstractButton::clicked, this, [this] {
        const BuildRecord r = app_->builder()->record(spec_.id);
        FolderRow::revealInFileManager(QFileInfo(r.artifactPath).absolutePath());
    });
    connect(&t, &LauncherTheme::changed, this, [this] { refresh(); });
    refresh();
}

QString TargetCard::statusText() const { return status_->text(); }

void TargetCard::refresh() {
    const LauncherTheme& t = LauncherTheme::instance();
    const bool isHost = spec_.id == app_->hostTarget();
    QString title = spec_.label;
    if (isHost) title += QStringLiteral(" (this machine, %1)").arg(QSysInfo::currentCpuArchitecture() == "arm64" ? QStringLiteral("Apple silicon") : QSysInfo::currentCpuArchitecture());
    title_->setText(title);
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    howTo_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    path_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    QString reason;
    const bool can = BuildRunner::hostCanBuild(spec_, &reason);
    const BuildRecord r = app_->builder()->record(spec_.id);
    const bool building = app_->builder()->running() && app_->builder()->runningTarget() == spec_.id;
    const bool busy = app_->builder()->running();
    progress_->setVisible(building);
    cancel_->setVisible(building);
    build_->setVisible(!building);
    details_->setVisible(!building && !r.failure.isEmpty());
    show_->setVisible(!building && r.ok && QFileInfo::exists(r.artifactPath));
    QString status, color = t.css(LauncherTheme::TextMuted);
    if (!can) {
        status = spec_.unavailableReason.isEmpty() ? QStringLiteral("Cannot be built on this machine: %1").arg(reason) : QStringLiteral("Not available in this release: %1").arg(reason);
        color = t.css(LauncherTheme::Warn);
        build_->setEnabled(false);
        build_->setText(QStringLiteral("Build"));
    } else {
        build_->setEnabled(!busy && (app_->installer()->complete() || !spec_.needs.isEmpty()));
        if (r.ok) {
            const bool current = app_->builder()->upToDate(spec_.id);
            status = QStringLiteral("Built %1 · build %2 · %3 · %4").arg(r.builtAt.toString("yyyy-MM-dd HH:mm"), r.sourceCommit.left(7), Installer::bytesText(r.artifactBytes),
                                                                      current ? QStringLiteral("up to date") : QStringLiteral("the source or toolchain changed since"));
            color = t.css(current ? LauncherTheme::Ok : LauncherTheme::Warn);
            build_->setText(QStringLiteral("Rebuild"));
        } else if (!r.failure.isEmpty()) {
            status = r.failure;
            color = t.css(LauncherTheme::Error);
            build_->setText(QStringLiteral("Retry"));
        } else {
            status = spec_.needs.contains("vitasdk") && !app_->installer()->vitaSdkInstalled()
                         ? QStringLiteral("Produces %1. VitaSDK is installed into your app-data folder the first time (about 900 MB).").arg(spec_.artifactPath.isEmpty() ? QStringLiteral("the artefact") : spec_.artifactPath)
                         : QStringLiteral("Not built yet.");
            build_->setText(spec_.artifact == "vpk" ? QStringLiteral("Build .vpk") : spec_.artifact == "zip" ? QStringLiteral("Build .zip") : QStringLiteral("Build"));
        }
    }
    status_->setText(status);
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    path_->setText(r.ok ? AppData::shortPath(r.artifactPath) : AppData::shortPath(app_->builder()->buildDir(spec_.id)));
    build_->setAccessibleName(QStringLiteral("%1 %2").arg(build_->text(), spec_.label));
}

void TargetCard::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.setBrush(t.color(LauncherTheme::Surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
}

}  // namespace ps2r::launcher
