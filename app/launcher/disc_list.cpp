// app/launcher/disc_list.cpp -- the shared disc cards panel.
#include "disc_list.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QVBoxLayout>
#include "app_data.h"
#include "empty_state.h"
#include "flat_button.h"
#include "game_card.h"
#include "game_library.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "title_manifest.h"
namespace ps2r::launcher {

DiscList::DiscList(LauncherApp* app, bool selectable, QWidget* parent) : QWidget(parent), app_(app), selectable_(selectable) {
    const LauncherTheme& t = LauncherTheme::instance();
    setAcceptDrops(true);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    cardsHost_ = new QWidget(this);
    cardsLayout_ = new QVBoxLayout(cardsHost_);
    cardsLayout_->setContentsMargins(0, 0, 0, 0);
    cardsLayout_->setSpacing(LauncherTheme::S3);
    col->addWidget(cardsHost_);
    Q_UNUSED(t);
    empty_ = new EmptyState(QStringLiteral("No game yet"),
                            QStringLiteral("Add your own disc image of %1 and the launcher builds the game from it. A disc image is a copy of your disc, usually a .iso file; you can also drop it here.")
                                .arg(app->manifest()->name()),
                            QStringLiteral("Add a game…"), QString(), this);
    empty_->setObjectName(QStringLiteral("empty_state"));
    col->addWidget(empty_);
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(LauncherTheme::S2);
    bottom->addStretch(1);
    add_ = new FlatButton(QStringLiteral("Add a game…"), FlatButton::Secondary, this);
    bottom->addWidget(add_);
    col->addLayout(bottom);

    connect(empty_, &EmptyState::primaryClicked, this, &DiscList::addRequested);
    connect(add_, &QAbstractButton::clicked, this, &DiscList::addRequested);
    connect(app_->library(), &GameLibrary::changed, this, &DiscList::rebuildCards);
    connect(app_, &LauncherApp::stateChanged, this, &DiscList::refreshCards);
    rebuildCards();
}

GameCard* DiscList::card(const QString& isoPath) const {
    for (GameCard* c : cards_) if (c->isoPath() == isoPath) return c;
    return nullptr;
}

void DiscList::rebuildCards() {
    for (GameCard* c : cards_) { cardsLayout_->removeWidget(c); c->deleteLater(); }
    cards_.clear();
    // the supported discs first, then the recognised-but-not-supported ones
    QVector<GameEntry> entries = app_->library()->games();
    std::stable_sort(entries.begin(), entries.end(), [](const GameEntry& a, const GameEntry& b) { return a.identity.playable() && !b.identity.playable(); });
    for (const GameEntry& g : entries) {
        auto* c = new GameCard(app_, g.path, cardsHost_);
        c->setObjectName(QStringLiteral("card_%1").arg(cards_.size()));
        c->setSelectable(selectable_);
        connect(c, &GameCard::actionRequested, this, [this, path = g.path] { emit actionRequested(path); });
        connect(c, &GameCard::selectRequested, this, [this, path = g.path] { setSelectedDisc(path); });
        connect(c, &GameCard::removeRequested, this, [this, path = g.path] { app_->library()->remove(path); });
        cards_ << c;
        cardsLayout_->addWidget(c);
    }
    if (selectable_) {
        if (!selected_.isEmpty() && (!card(selected_) || !card(selected_)->supported())) selected_.clear();
        if (selected_.isEmpty()) {   // the first supported disc is selected by itself
            for (GameCard* c : cards_) if (c->supported()) { selected_ = c->isoPath(); break; }
            emit selectionChanged(selected_);
        }
        for (GameCard* c : cards_) c->setSelected(c->isoPath() == selected_);
    }
    empty_->setVisible(cards_.isEmpty());
    if (cards_.isEmpty()) empty_->setTone(EmptyState::Neutral);
    cardsHost_->setVisible(!cards_.isEmpty());
}

void DiscList::refreshCards() {
    for (GameCard* c : cards_) c->refresh();
}

void DiscList::setSelectedDisc(const QString& isoPath) {
    if (selected_ == isoPath) return;
    selected_ = isoPath;
    for (GameCard* c : cards_) c->setSelected(c->isoPath() == selected_);
    emit selectionChanged(selected_);
}

void DiscList::addDiscFromPath(const QString& path) {
    const DiscIdentity d = app_->addDisc(path);
    if (!d.recognised()) { showDiscError(d.reason); return; }
    if (selectable_ && d.playable()) setSelectedDisc(path);
}

void DiscList::showDiscError(const QString& reason) {
    empty_->setTone(EmptyState::Error);
    const int cut = reason.indexOf(QStringLiteral(". "));
    const QString title = cut > 0 ? reason.left(cut) : QStringLiteral("That did not work");
    const QString body = cut > 0 ? reason.mid(cut + 2) : reason;
    empty_->setTexts(title, body);
    empty_->setVisible(true);
}

void DiscList::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) { event->acceptProposedAction(); dragOver_ = true; update(); }
}

void DiscList::dragLeaveEvent(QDragLeaveEvent*) { dragOver_ = false; update(); }

void DiscList::dropEvent(QDropEvent* event) {
    dragOver_ = false;
    update();
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile()) { addDiscFromPath(u.toLocalFile()); break; }
    }
    event->acceptProposedAction();
}

void DiscList::paintEvent(QPaintEvent*) {
    if (!dragOver_) return;
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(t.color(LauncherTheme::Accent), 2, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
    p.setFont(t.font(LauncherTheme::Title));
    p.setPen(t.color(LauncherTheme::Accent));
    p.drawText(rect().adjusted(0, 0, 0, -LauncherTheme::S5), Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral("Drop the disc image to add it"));
}

}  // namespace ps2r::launcher
