// app/launcher/save_states_page.cpp -- the Save States page.
#include "save_states_page.h"
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "app_data.h"
#include "empty_state.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_folder.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "save_state_card.h"
#include "save_state_list.h"
#include "title_manifest.h"
namespace ps2r::launcher {

SaveStatesPage::SaveStatesPage(LauncherApp* app, QWidget* parent) : QWidget(parent), app_(app) {
    const LauncherTheme& t = LauncherTheme::instance();
    list_ = new SaveStateList(app->manifest(), app->gameFolder());
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    scroll->setWidget(inner);
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(LauncherTheme::S5, 0, LauncherTheme::S5, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QStringLiteral("Save States"), QStringLiteral("A save state is the whole game frozen at one moment. Resume one to carry on from there."), inner));
    cardsHost_ = new QWidget(inner);
    cardsLayout_ = new QVBoxLayout(cardsHost_);
    cardsLayout_->setContentsMargins(0, 0, 0, 0);
    cardsLayout_->setSpacing(LauncherTheme::S3);
    col->addWidget(cardsHost_);
    empty_ = new EmptyState(QStringLiteral("No save states yet"),
                            QStringLiteral("While playing, press F5 to freeze the game at that moment; it appears here. Press F8 in the game to go back to the last one."),
                            QStringLiteral("Refresh"), QString(), inner);
    empty_->setObjectName(QStringLiteral("save_states_empty"));
    col->addWidget(empty_);
    error_ = new QLabel(inner);
    error_->setFont(t.font(LauncherTheme::Body));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Error)));
    error_->hide();
    col->addWidget(error_);
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(LauncherTheme::S2);
    note_ = new QLabel(inner);
    note_->setFont(t.font(LauncherTheme::Caption));
    note_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    bottom->addWidget(note_, 1);
    open_ = new FlatButton(QStringLiteral("Open the folder"), FlatButton::Secondary, inner);
    refresh_ = new FlatButton(QStringLiteral("Refresh"), FlatButton::Quiet, inner);
    bottom->addWidget(open_);
    bottom->addWidget(refresh_);
    col->addLayout(bottom);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(empty_, &EmptyState::primaryClicked, this, &SaveStatesPage::refresh);
    connect(refresh_, &QAbstractButton::clicked, this, &SaveStatesPage::refresh);
    connect(open_, &QAbstractButton::clicked, this, [this] {
        const QString dir = list_->folder();
        if (dir.isEmpty()) return;
        QDir().mkpath(dir);
        FolderRow::revealInFileManager(dir);
    });
    connect(app_, &LauncherApp::foldersChanged, this, &SaveStatesPage::refresh);
    refresh();
}

SaveStatesPage::~SaveStatesPage() { delete list_; }

QString SaveStatesPage::errorText() const { return error_->isVisible() ? error_->text() : QString(); }

void SaveStatesPage::refresh() {
    for (SaveStateCard* c : cards_) { cardsLayout_->removeWidget(c); c->deleteLater(); }
    cards_.clear();
    error_->hide();
    const bool chosen = app_->gameFolder()->chosen();
    list_->scan();
    for (const SaveStateEntry& e : list_->entries()) {
        auto* c = new SaveStateCard(e, cardsHost_);
        c->setObjectName(QStringLiteral("state_%1").arg(cards_.size()));
        connect(c, &SaveStateCard::resumeRequested, this, [this, path = e.path] { resume(path); });
        cards_ << c;
        cardsLayout_->addWidget(c);
    }
    if (!chosen) {
        empty_->setTexts(QStringLiteral("Choose where the game lives first"), QStringLiteral("Save states are kept in the game folder, beside the game."));
    } else {
        empty_->setTexts(QStringLiteral("No save states yet"),
                         QStringLiteral("While playing, press F5 to freeze the game at that moment; it appears here. Press F8 in the game to go back to the last one."));
    }
    empty_->setTone(EmptyState::Neutral);
    empty_->setVisible(cards_.isEmpty());
    cardsHost_->setVisible(!cards_.isEmpty());
    open_->setEnabled(chosen);
    note_->setText(chosen ? QStringLiteral("%1 save state%2 in %3").arg(cards_.size()).arg(cards_.size() == 1 ? "" : "s").arg(AppData::shortPath(list_->folder())) : QString());
}

void SaveStatesPage::resume(const QString& statePath) {
    QString err;
    if (!app_->play(app_->currentDisc(), &err, statePath)) {
        error_->setText(err);
        error_->show();
    }
}

void SaveStatesPage::showEvent(QShowEvent* event) {
    refresh();
    QWidget::showEvent(event);
}

}  // namespace ps2r::launcher
