// app/launcher/games_page.cpp -- the Games page.
#include "games_page.h"
#include <QDragEnterEvent>
#include "app_data.h"
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include "build_runner.h"
#include "details_sheet.h"
#include "disc_scanner.h"
#include "empty_state.h"
#include "first_run_sheet.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_card.h"
#include "game_library.h"
#include "game_process.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "progress_panel.h"
#include "title_manifest.h"
namespace ps2r::launcher {

GamesPage::GamesPage(LauncherApp* app, DetailsSheet* details, QWidget* parent) : QWidget(parent), app_(app), details_(details) {
    const LauncherTheme& t = LauncherTheme::instance();
    setAcceptDrops(true);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    scroll_->setWidget(inner);
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(LauncherTheme::S5, 0, LauncherTheme::S5, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QStringLiteral("Games"), QStringLiteral("Your disc images, found by what is on the disc, never by the file name."), inner));
    cardsHost_ = new QWidget(inner);
    cardsLayout_ = new QVBoxLayout(cardsHost_);
    cardsLayout_->setContentsMargins(0, 0, 0, 0);
    cardsLayout_->setSpacing(LauncherTheme::S3);
    col->addWidget(cardsHost_);
    QStringList places;
    for (const QString& f : app->manifest()->scanFolders()) {
        const QString name = QFileInfo(app->appData()->expand(f, app->folder("games"))).fileName();
        if (!name.isEmpty()) places << name;
    }
    empty_ = new EmptyState(QStringLiteral("No game found — drop your disc image here"),
                            QStringLiteral("%1 and removable drives were searched for a disc of %2.").arg(places.join(", "), app->manifest()->name()),
                            QStringLiteral("Choose a disc image…"), QStringLiteral("Search again"), inner);
    empty_->setObjectName(QStringLiteral("empty_state"));
    col->addWidget(empty_);
    auto* foldersTitle = new QLabel(QStringLiteral("Folders"), inner);
    foldersTitle->setFont(t.font(LauncherTheme::Title));
    col->addSpacing(LauncherTheme::S2);
    col->addWidget(foldersTitle);
    for (const FolderSpec& f : app->manifest()->folders()) {
        auto* row = new FolderRow(f.label, inner);
        row->setObjectName(QStringLiteral("folder_%1").arg(f.id));
        row->setProperty("folder_id", f.id);
        connect(row, &FolderRow::changeRequested, this, [this, id = f.id] { changeFolder(id); });
        folders_ << row;
        col->addWidget(row);
    }
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(LauncherTheme::S2);
    scanNote_ = new QLabel(inner);
    scanNote_->setFont(t.font(LauncherTheme::Caption));
    scanNote_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    bottom->addWidget(scanNote_, 1);
    add_ = new FlatButton(QStringLiteral("Add a disc image…"), FlatButton::Secondary, inner);
    rescan_ = new FlatButton(QStringLiteral("Rescan"), FlatButton::Quiet, inner);
    bottom->addWidget(add_);
    bottom->addWidget(rescan_);
    col->addSpacing(LauncherTheme::S2);
    col->addLayout(bottom);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll_);

    connect(empty_, &EmptyState::primaryClicked, this, &GamesPage::chooseDisc);
    connect(empty_, &EmptyState::quietClicked, this, [this] { app_->scan(); });
    connect(add_, &QAbstractButton::clicked, this, &GamesPage::chooseDisc);
    connect(rescan_, &QAbstractButton::clicked, this, [this] { app_->scan(); });
    connect(app_->library(), &GameLibrary::changed, this, &GamesPage::rebuildCards);
    connect(app_, &LauncherApp::stateChanged, this, &GamesPage::refreshCards);
    connect(app_, &LauncherApp::foldersChanged, this, &GamesPage::refreshFolders);
    connect(app_->scanner(), &DiscScanner::finished, this, [this](int n, int) {
        scanNote_->setText(n == 0 ? QStringLiteral("No disc image of %1 was found in the usual places.").arg(app_->manifest()->name())
                                  : QStringLiteral("%1 disc image%2 found.").arg(n).arg(n == 1 ? "" : "s"));
        rebuildCards();
    });
    connect(app_->builder(), &BuildRunner::progress, this, [this](const QString& text, int done, int total, qint64 elapsed, qint64 remaining) {
        for (GameCard* c : cards_) {
            c->progress()->setText(QStringLiteral("Building for this machine — %1").arg(text));
            c->progress()->setProgress(done, total);
            QString cap = QStringLiteral("%1 elapsed").arg(BuildRunner::durationText(elapsed));
            if (remaining >= 0) cap += QStringLiteral(" · about %1 left").arg(BuildRunner::durationText(remaining));
            c->progress()->setCaption(cap);
        }
    });
    connect(app_->builder(), &BuildRunner::logLine, this, [this](const QString& l) { details_->appendLine(l); });
    connect(app_->builder(), &BuildRunner::finished, this, [this](const QString&, bool, const BuildRecord&) { refreshCards(); });
    auto* ticker = new QTimer(this);
    ticker->setInterval(1000);
    connect(ticker, &QTimer::timeout, this, [this] {
        if (app_->game()->running()) refreshCards();
        if (app_->builder()->running() && app_->builder()->runningTarget() == app_->hostTarget()) {
            // the configure phase prints no ninja lines: the elapsed time still moves
            for (GameCard* c : cards_) {
                const QString cap = c->progress()->caption();
                if (!cap.contains(QStringLiteral("left"))) c->progress()->setCaption(QStringLiteral("%1 elapsed").arg(BuildRunner::durationText(app_->builder()->elapsedSeconds())));
            }
        }
    });
    ticker->start();
    refreshFolders();
    rebuildCards();
}

FolderRow* GamesPage::folderRow(const QString& id) const {
    for (FolderRow* r : folders_) if (r->property("folder_id").toString() == id) return r;
    return nullptr;
}

GameCard* GamesPage::card(const QString& isoPath) const {
    for (GameCard* c : cards_) if (c->isoPath() == isoPath) return c;
    return nullptr;
}

void GamesPage::refreshFolders() {
    for (FolderRow* r : folders_) r->setPath(app_->folder(r->property("folder_id").toString()));
}

void GamesPage::rebuildCards() {
    for (GameCard* c : cards_) { cardsLayout_->removeWidget(c); c->deleteLater(); }
    cards_.clear();
    for (const GameEntry& g : app_->library()->games()) {
        auto* c = new GameCard(app_, g.path, cardsHost_);
        c->setObjectName(QStringLiteral("card_%1").arg(cards_.size()));
        connect(c, &GameCard::actionRequested, this, [this, path = g.path] { runCardAction(path); });
        connect(c, &GameCard::detailsRequested, this, [this] {
            const BuildRecord r = app_->builder()->record(app_->hostTarget());
            details_->showLogFile(QStringLiteral("Build details"), app_->builder()->currentLogPath());
            Q_UNUSED(r);
        });
        connect(c, &GameCard::cancelRequested, this, [this] { app_->builder()->cancel(); });
        connect(c, &GameCard::removeRequested, this, [this, path = g.path] { app_->library()->remove(path); });
        cards_ << c;
        cardsLayout_->addWidget(c);
    }
    empty_->setVisible(cards_.isEmpty());
    empty_->setTone(EmptyState::Neutral);
    cardsHost_->setVisible(!cards_.isEmpty());
}

void GamesPage::refreshCards() {
    for (GameCard* c : cards_) c->refresh();
}

void GamesPage::runCardAction(const QString& isoPath) {
    switch (app_->nextAction(isoPath)) {
    case LauncherApp::Missing: app_->library()->remove(isoPath); break;
    case LauncherApp::Install:
        if (firstRun_) firstRun_->open();
        app_->install();
        break;
    case LauncherApp::Build:
        app_->build(app_->hostTarget());
        refreshCards();
        break;
    case LauncherApp::Play: {
        QString err;
        if (!app_->play(isoPath, &err)) showDiscError(err);
        break;
    }
    case LauncherApp::Playing: break;
    }
}

void GamesPage::chooseDisc() {
    QStringList filters;
    for (const QString& e : app_->manifest()->discExtensions()) filters << "*" + e;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a disc image of %1").arg(app_->manifest()->name()),
                                                      app_->folder("games"), QStringLiteral("Disc images (%1)").arg(filters.join(' ')));
    if (!path.isEmpty()) addDiscFromPath(path);
}

void GamesPage::addDiscFromPath(const QString& path) {
    QString reason;
    if (!app_->addDisc(path, &reason)) showDiscError(reason);
}

void GamesPage::showDiscError(const QString& reason) {
    empty_->setTone(EmptyState::Error);
    const int cut = reason.indexOf(". ");
    const QString title = cut > 0 ? reason.left(cut) : QStringLiteral("That did not work");
    const QString body = cut > 0 ? reason.mid(cut + 2) : reason;
    empty_->setTexts(title, body);
    empty_->setVisible(true);
}

void GamesPage::changeFolder(const QString& id) {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose the %1 folder").arg(QString(id).replace('_', ' ')), app_->folder(id));
    if (dir.isEmpty()) return;
    if (id == "game_output") app_->chooseGameFolder(dir);   // a folder of the title's name inside it, the game copied there
    else app_->setFolder(id, dir);
}

void GamesPage::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) { event->acceptProposedAction(); dragOver_ = true; update(); }
}

void GamesPage::dragLeaveEvent(QDragLeaveEvent*) { dragOver_ = false; update(); }

void GamesPage::dropEvent(QDropEvent* event) {
    dragOver_ = false;
    update();
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile()) { addDiscFromPath(u.toLocalFile()); break; }
    }
    event->acceptProposedAction();
}

void GamesPage::paintEvent(QPaintEvent*) {
    if (!dragOver_) return;
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(t.color(LauncherTheme::Accent), 2, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(LauncherTheme::S3, LauncherTheme::S3, -LauncherTheme::S3, -LauncherTheme::S3), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
    p.setFont(t.font(LauncherTheme::Title));
    p.setPen(t.color(LauncherTheme::Accent));
    p.drawText(rect().adjusted(0, 0, 0, -LauncherTheme::S5), Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral("Drop the disc image to add it"));
}

}  // namespace ps2r::launcher
