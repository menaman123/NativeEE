// app/launcher/game_card.cpp -- the game card, painted.
#include "game_card.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>
#include "build_runner.h"
#include "flat_button.h"
#include "game_library.h"
#include "game_process.h"
#include "generated_artwork.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "progress_panel.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
constexpr int kCardHeight = 180;
constexpr int kArt = 148;
}  // namespace

GameCard::GameCard(LauncherApp* app, const QString& isoPath, QWidget* parent) : QWidget(parent), app_(app), iso_(isoPath) {
    const LauncherTheme& t = LauncherTheme::instance();
    setMinimumHeight(kCardHeight);   // taller when the status text wraps
    setAccessibleName(QStringLiteral("%1 card").arg(app->manifest()->name()));
    auto* art = new GeneratedArtwork(app->manifest()->name(), app->manifest()->id() + isoPath, kArt, this);
    title_ = new QLabel(app->manifest()->name(), this);
    title_->setFont(t.font(LauncherTheme::Title));
    subtitle_ = new QLabel(this);
    subtitle_->setFont(t.font(LauncherTheme::Body));
    status_ = new QLabel(this);
    status_->setFont(t.font(LauncherTheme::Body));
    status_->setWordWrap(true);
    facts_ = new QLabel(this);
    facts_->setFont(t.font(LauncherTheme::Caption));
    facts_->setWordWrap(true);
    action_ = new FlatButton(QStringLiteral("Play"), FlatButton::Primary, this);
    action_->setLarge(true);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet, this);
    cancel_ = new FlatButton(QStringLiteral("Cancel"), FlatButton::Quiet, this);
    progress_ = new ProgressPanel(this);
    progress_->hide();
    actionRow_ = new QWidget(this);
    auto* actions = new QHBoxLayout(actionRow_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(LauncherTheme::S2);
    actions->addStretch(1);
    actions->addWidget(details_);
    actions->addWidget(cancel_);
    actions->addWidget(action_);
    auto* text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(LauncherTheme::S1);
    text->addWidget(title_);
    text->addWidget(subtitle_);
    text->addWidget(status_);
    text->addWidget(facts_);
    text->addStretch(1);
    text->addWidget(progress_);
    text->addWidget(actionRow_);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3);
    row->setSpacing(LauncherTheme::S3);
    row->addWidget(art, 0, Qt::AlignTop);
    row->addLayout(text, 1);
    connect(action_, &QAbstractButton::clicked, this, &GameCard::actionRequested);
    connect(details_, &QAbstractButton::clicked, this, &GameCard::detailsRequested);
    connect(cancel_, &QAbstractButton::clicked, this, &GameCard::cancelRequested);
    connect(&t, &LauncherTheme::changed, this, [this] { restyle(); update(); });
    refresh();
}

QString GameCard::statusText() const { return status_->text(); }
QString GameCard::factsText() const { return facts_->text(); }

void GameCard::refresh() {
    const LauncherTheme& t = LauncherTheme::instance();
    const GameEntry* g = app_->library()->game(iso_);
    if (!g) return;
    subtitle_->setText(LauncherApp::cardSubtitle(*app_->manifest(), g->fingerprintFile, iso_));
    const LauncherApp::Action next = app_->nextAction(iso_);
    const BuildRecord rec = app_->builder()->record(app_->hostTarget());
    const bool building = app_->builder()->running() && app_->builder()->runningTarget() == app_->hostTarget();
    QString status, statusColor = t.css(LauncherTheme::TextMuted), build;
    if (app_->buildFromSource() && rec.ok) build = QStringLiteral("build %1").arg(rec.sourceCommit.left(7));
    else if (!app_->buildFromSource() && !app_->installer()->installedRelease().isEmpty()) build = app_->installer()->installedRelease();
    if (building) {
        progress_->show();
        actionRow_->setVisible(true);
        action_->hide();
        details_->show();
        cancel_->show();
        status_->hide();
    } else {
        progress_->hide();
        progress_->clear();
        status_->show();
        action_->show();
        details_->setVisible(next != LauncherApp::Playing && !rec.ok && !rec.failure.isEmpty());
        cancel_->hide();
        switch (next) {
        case LauncherApp::Missing:
            status = QStringLiteral("The disc image is no longer at this path.");
            statusColor = t.css(LauncherTheme::Error);
            action_->setText(QStringLiteral("Remove"));
            action_->setKind(FlatButton::Secondary);
            break;
        case LauncherApp::Install:
            status = app_->buildFromSource() ? QStringLiteral("The first run has not finished: the tools the build needs are not installed yet.")
                                             : QStringLiteral("Not installed yet: Install downloads the finished game.");
            action_->setText(QStringLiteral("Install"));
            action_->setKind(FlatButton::Primary);
            break;
        case LauncherApp::Build:
            if (!rec.failure.isEmpty() && !rec.ok) { status = rec.failure; statusColor = t.css(LauncherTheme::Error); }
            else if (rec.ok) { status = QStringLiteral("The game's source or toolchain changed since the last build."); statusColor = t.css(LauncherTheme::Warn); build += QStringLiteral(" (needs a build)"); }
            else status = QStringLiteral("Not built for this machine yet — Build compiles the game from its source (about ten minutes).");
            action_->setText(rec.ok || !rec.failure.isEmpty() ? QStringLiteral("Rebuild") : QStringLiteral("Build"));
            action_->setKind(FlatButton::Primary);
            break;
        case LauncherApp::Play:
            status = QStringLiteral("Ready to play.");
            statusColor = t.css(LauncherTheme::Ok);
            if (!build.isEmpty()) build += QStringLiteral(" (up to date)");
            action_->setText(QStringLiteral("Play"));
            action_->setKind(FlatButton::Primary);
            break;
        case LauncherApp::Playing:
            status = QStringLiteral("Playing… %1").arg(BuildRunner::durationText(app_->game()->elapsedSeconds()));
            action_->setText(QStringLiteral("Playing…"));
            action_->setKind(FlatButton::Primary);
            break;
        }
        action_->setEnabled(next != LauncherApp::Playing);
        action_->setAccessibleName(action_->text());
    }
    status_->setText(status);
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor));
    QStringList facts;
    facts << GameLibrary::relativeTime(g->lastPlayed);
    if (g->playtimeSeconds > 0) facts << GameLibrary::playtimeText(g->playtimeSeconds);
    if (!build.isEmpty()) facts << build;
    facts_->setText(facts.join(QStringLiteral(" · ")));
    restyle();
}

void GameCard::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    subtitle_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    facts_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

void GameCard::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.color(LauncherTheme::Line), 1));
    p.setBrush(hover_ ? t.color(LauncherTheme::SurfaceRaised) : t.color(LauncherTheme::Surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
}

void GameCard::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void GameCard::leaveEvent(QEvent*) { hover_ = false; update(); }

void GameCard::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* remove = menu.addAction(QStringLiteral("Remove from the list"));
    if (menu.exec(event->globalPos()) == remove) emit removeRequested();
}

}  // namespace ps2r::launcher
