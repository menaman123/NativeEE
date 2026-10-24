// app/launcher/disc_list.cpp -- the shared disc cards panel.
#include "disc_list.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QVBoxLayout>
#include "app_data.h"
#include "disc_scanner.h"
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
    QStringList places;
    for (const QString& f : app->manifest()->scanFolders()) {
        const QString name = QFileInfo(app->appData()->expand(f)).fileName();
        if (!name.isEmpty()) places << name;
    }
    empty_ = new EmptyState(QStringLiteral("No disc image found — drop yours here"),
                            QStringLiteral("%1 and removable drives were searched for a disc of %2. A disc image is a copy of your own disc, usually a .iso file.")
                                .arg(places.join(QStringLiteral(", ")), app->manifest()->name()),
                            QStringLiteral("Choose a disc image…"), QStringLiteral("Search again"), this);
    empty_->setObjectName(QStringLiteral("empty_state"));
    col->addWidget(empty_);
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(LauncherTheme::S2);
    scanNote_ = new QLabel(this);
    scanNote_->setFont(t.font(LauncherTheme::Caption));
    scanNote_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    bottom->addWidget(scanNote_, 1);
    add_ = new FlatButton(QStringLiteral("Add a disc image…"), FlatButton::Secondary, this);
    rescan_ = new FlatButton(QStringLiteral("Search again"), FlatButton::Quiet, this);
    bottom->addWidget(add_);
    bottom->addWidget(rescan_);
    col->addLayout(bottom);

    connect(empty_, &EmptyState::primaryClicked, this, &DiscList::chooseDisc);
    connect(empty_, &EmptyState::quietClicked, this, [this] { app_->scan(); });
    connect(add_, &QAbstractButton::clicked, this, &DiscList::chooseDisc);
    connect(rescan_, &QAbstractButton::clicked, this, [this] { app_->scan(); });
    connect(app_->library(), &GameLibrary::changed, this, &DiscList::rebuildCards);
    connect(app_, &LauncherApp::stateChanged, this, &DiscList::refreshCards);
    connect(app_->scanner(), &DiscScanner::finished, this, [this](int n, int) {
        scanNote_->setText(n == 0 ? QStringLiteral("No disc image of %1 was found in the usual places.").arg(app_->manifest()->name())
                                  : QStringLiteral("%1 disc image%2 found.").arg(n).arg(n == 1 ? "" : "s"));
        rebuildCards();
    });
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

void DiscList::chooseDisc() {
    QStringList filters;
    for (const QString& e : app_->manifest()->discExtensions()) filters << "*" + e;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a disc image of %1").arg(app_->manifest()->name()),
                                                      AppData::homePath(), QStringLiteral("Disc images (%1)").arg(filters.join(' ')));
    if (!path.isEmpty()) addDiscFromPath(path);
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
