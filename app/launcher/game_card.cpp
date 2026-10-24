// app/launcher/game_card.cpp -- the game card, painted.
#include "game_card.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include "disc_identifier.h"
#include "flat_button.h"
#include "game_folder.h"
#include "game_library.h"
#include "game_process.h"
#include "generated_artwork.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
constexpr int kCardHeight = 164;
constexpr int kArt = 132;

QString durationText(qint64 seconds) {
    if (seconds < 60) return QStringLiteral("%1 s").arg(seconds);
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h == 0) return QStringLiteral("%1 min").arg(m);
    return QStringLiteral("%1 h %2 min").arg(h).arg(m);
}
}  // namespace

GameCard::GameCard(LauncherApp* app, const QString& isoPath, QWidget* parent) : QWidget(parent), app_(app), iso_(isoPath) {
    const LauncherTheme& t = LauncherTheme::instance();
    setMinimumHeight(kCardHeight);   // taller when the status text wraps
    setAccessibleName(QStringLiteral("%1 card").arg(app->manifest()->name()));
    setFocusPolicy(Qt::StrongFocus);
    auto* art = new GeneratedArtwork(app->manifest()->name(), app->manifest()->id() + isoPath, kArt, this);
    title_ = new QLabel(app->manifest()->name(), this);
    title_->setFont(t.font(LauncherTheme::Title));
    subtitle_ = new QLabel(this);
    subtitle_->setFont(t.font(LauncherTheme::Body));
    badge_ = new QLabel(QStringLiteral("Not supported yet"), this);
    badge_->setFont(t.font(LauncherTheme::Caption));
    badge_->setAccessibleName(QStringLiteral("Not supported yet"));
    badge_->hide();
    status_ = new QLabel(this);
    status_->setFont(t.font(LauncherTheme::Body));
    status_->setWordWrap(true);
    facts_ = new QLabel(this);
    facts_->setFont(t.font(LauncherTheme::Caption));
    facts_->setWordWrap(true);
    action_ = new FlatButton(QStringLiteral("Play"), FlatButton::Primary, this);
    action_->setLarge(true);
    actionRow_ = new QWidget(this);
    auto* actions = new QHBoxLayout(actionRow_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(LauncherTheme::S2);
    actions->addStretch(1);
    actions->addWidget(action_);
    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(LauncherTheme::S2);
    titleRow->addWidget(title_);
    titleRow->addWidget(badge_);
    titleRow->addStretch(1);
    auto* text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(LauncherTheme::S1);
    text->addLayout(titleRow);
    text->addWidget(subtitle_);
    text->addWidget(status_);
    text->addWidget(facts_);
    text->addStretch(1);
    text->addWidget(actionRow_);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3, LauncherTheme::S3);
    row->setSpacing(LauncherTheme::S3);
    row->addWidget(art, 0, Qt::AlignTop);
    row->addLayout(text, 1);
    connect(action_, &QAbstractButton::clicked, this, &GameCard::actionRequested);
    connect(&t, &LauncherTheme::changed, this, [this] { restyle(); update(); });
    refresh();
}

QString GameCard::statusText() const { return status_->text(); }
QString GameCard::factsText() const { return facts_->text(); }
QString GameCard::badgeText() const { return badge_->isHidden() ? QString() : badge_->text(); }

bool GameCard::supported() const {
    const GameEntry* g = app_->library()->game(iso_);
    return g && g->identity.playable();
}

void GameCard::setSelectable(bool on) {
    selectable_ = on;
    actionRow_->setVisible(!on);
    setCursor(on && supported() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    refresh();
}

void GameCard::setSelected(bool on) {
    selected_ = on;
    update();
}

void GameCard::refresh() {
    const LauncherTheme& t = LauncherTheme::instance();
    const GameEntry* g = app_->library()->game(iso_);
    if (!g) return;
    subtitle_->setText(DiscIdentifier::subtitle(g->identity));
    const LauncherApp::Action next = app_->nextAction(iso_);
    QString status, statusColor = t.css(LauncherTheme::TextMuted);
    badge_->setVisible(next == LauncherApp::Unsupported);
    action_->setVisible(!selectable_ && next != LauncherApp::Unsupported);
    switch (next) {
    case LauncherApp::Missing:
        status = QStringLiteral("The disc image is no longer at this path.");
        statusColor = t.css(LauncherTheme::Error);
        action_->setText(QStringLiteral("Remove"));
        action_->setKind(FlatButton::Secondary);
        break;
    case LauncherApp::Unsupported:
        status = g->identity.reason;
        statusColor = t.css(LauncherTheme::Warn);
        break;
    case LauncherApp::NoFolder:
        status = selectable_ ? QStringLiteral("Your disc. It is prepared in the next steps.") : QStringLiteral("Choose where the game should live, then prepare it.");
        action_->setText(QStringLiteral("Choose a folder…"));
        action_->setKind(FlatButton::Primary);
        break;
    case LauncherApp::Prepare: {
        const PreparedRecord r = app_->gameFolder()->prepared();
        if (selectable_) status = QStringLiteral("Your disc. It is prepared in the next steps.");
        else if (app_->preparing()) status = QStringLiteral("Preparing your game…");
        else if (r.ok) { status = QStringLiteral("Prepared by an older version of the launcher. Prepare it again to play with this one."); statusColor = t.css(LauncherTheme::Warn); }
        else status = QStringLiteral("Not prepared yet. Preparing reads your disc and makes the game for this computer; it takes a few minutes once.");
        action_->setText(QStringLiteral("Prepare"));
        action_->setKind(FlatButton::Primary);
        action_->setEnabled(!app_->preparing());
        break;
    }
    case LauncherApp::Play:
        status = QStringLiteral("Ready to play.");
        statusColor = t.css(LauncherTheme::Ok);
        action_->setText(QStringLiteral("Play"));
        action_->setKind(FlatButton::Primary);
        action_->setEnabled(true);
        break;
    case LauncherApp::Playing:
        status = QStringLiteral("Playing… %1").arg(durationText(app_->game()->elapsedSeconds()));
        action_->setText(QStringLiteral("Playing…"));
        action_->setKind(FlatButton::Primary);
        action_->setEnabled(false);
        break;
    }
    action_->setAccessibleName(action_->text());
    status_->setText(status);
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor));
    QStringList facts;
    if (next != LauncherApp::Unsupported) {
        facts << GameLibrary::relativeTime(g->lastPlayed);
        if (g->playtimeSeconds > 0) facts << GameLibrary::playtimeText(g->playtimeSeconds);
        const PreparedRecord r = app_->gameFolder()->prepared();
        if (r.ok && r.when.isValid()) facts << QStringLiteral("Prepared %1").arg(r.when.toString("yyyy-MM-dd HH:mm"));
    } else {
        facts << QStringLiteral("%1 GB").arg(QString::number(g->sizeBytes / (1024.0 * 1024 * 1024), 'f', 2));
    }
    facts_->setText(facts.join(QStringLiteral(" · ")));
    restyle();
}

void GameCard::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    subtitle_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    facts_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    badge_->setStyleSheet(QStringLiteral("color: %1; background: %2; border-radius: 9px; padding: 1px 8px;")
                              .arg(t.css(LauncherTheme::Warn), t.dark() ? QStringLiteral("#3A3220") : QStringLiteral("#FBF1DC")));
}

void GameCard::paintEvent(QPaintEvent*) {
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool ring = selectable_ && selected_;
    p.setPen(QPen(ring ? t.color(LauncherTheme::Accent) : t.color(LauncherTheme::Line), ring ? 2 : 1));
    p.setBrush(hover_ && (!selectable_ || supported()) ? t.color(LauncherTheme::SurfaceRaised) : t.color(LauncherTheme::Surface));
    const qreal inset = ring ? 1.0 : 0.5;
    p.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
    if (hasFocus() && selectable_) {
        p.setPen(QPen(t.color(LauncherTheme::Accent), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(3, 3, -3, -3), LauncherTheme::radiusCard() - 2, LauncherTheme::radiusCard() - 2);
    }
}

void GameCard::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void GameCard::leaveEvent(QEvent*) { hover_ = false; update(); }

void GameCard::mousePressEvent(QMouseEvent* event) {
    if (selectable_ && event->button() == Qt::LeftButton) {
        if (supported()) emit selectRequested();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GameCard::keyPressEvent(QKeyEvent* event) {
    if (selectable_ && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (supported()) emit selectRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void GameCard::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* remove = menu.addAction(QStringLiteral("Remove from the list"));
    if (menu.exec(event->globalPos()) == remove) emit removeRequested();
}

}  // namespace ps2r::launcher
