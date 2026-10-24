// app/launcher/games_page.cpp -- the Games page.
#include "games_page.h"
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include "details_sheet.h"
#include "disc_list.h"
#include "flat_button.h"
#include "game_library.h"
#include "game_process.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
namespace ps2r::launcher {

GamesPage::GamesPage(LauncherApp* app, DetailsSheet* details, QWidget* parent) : QWidget(parent), app_(app), details_(details) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    scroll->setWidget(inner);
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(LauncherTheme::S5, 0, LauncherTheme::S5, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QStringLiteral("Games"), QStringLiteral("Your disc images, found by what is on the disc, never by the file name."), inner));
    discs_ = new DiscList(app, false, inner);
    discs_->setObjectName(QStringLiteral("games_discs"));
    col->addWidget(discs_);
    auto* row = new QHBoxLayout();
    other_ = new FlatButton(QStringLiteral("Prepare for another computer…"), FlatButton::Quiet, inner);
    row->addWidget(other_);
    row->addStretch(1);
    col->addLayout(row);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(discs_, &DiscList::actionRequested, this, &GamesPage::runCardAction);
    connect(other_, &QAbstractButton::clicked, this, &GamesPage::otherComputerRequested);
    auto* ticker = new QTimer(this);
    ticker->setInterval(1000);
    connect(ticker, &QTimer::timeout, this, [this] { if (app_->game()->running()) discs_->refreshCards(); });
    ticker->start();
}

void GamesPage::runCardAction(const QString& isoPath) {
    switch (app_->nextAction(isoPath)) {
    case LauncherApp::Missing: app_->library()->remove(isoPath); break;
    case LauncherApp::NoFolder: app_->setCurrentDisc(isoPath); emit folderRequested(); break;
    case LauncherApp::Prepare: app_->setCurrentDisc(isoPath); emit prepareRequested(isoPath); break;
    case LauncherApp::Play: {
        QString err;
        if (!app_->play(isoPath, &err)) discs_->showDiscError(err);
        break;
    }
    case LauncherApp::Playing:
    case LauncherApp::Unsupported: break;
    }
}

}  // namespace ps2r::launcher
