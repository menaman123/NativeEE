// app/launcher/first_run_flow.cpp -- the first run.
#include "first_run_flow.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "app_data.h"
#include "details_sheet.h"
#include "disc_list.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_folder.h"
#include "host_platform.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "prepare_runner.h"
#include "progress_panel.h"
#include "segmented_control.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
constexpr int kPages = 5;
const char* kTitles[kPages] = {"Where is your game?", "Where should the game live?", "Which computer is it for?", "Preparing your game", "Ready to play"};
const char* kDescriptions[kPages] = {
    "Your disc images, found by what is on the disc, never by the file name. Pick the one to prepare.",
    "One folder holds the game, its options and its saves. Move the folder and the game moves with it.",
    "The game is made for one kind of computer. This one is chosen for you.",
    "This reads your disc and makes the game for your computer. It happens once; you can keep using the computer.",
    ""};

QString etaText(qint64 seconds) {
    if (seconds < 0) return QString();
    if (seconds < 60) return QStringLiteral("under a minute left");
    const qint64 m = (seconds + 30) / 60;
    return m == 1 ? QStringLiteral("about a minute left") : QStringLiteral("about %1 minutes left").arg(m);
}

QLabel* mutedLabel(const QString& text, QWidget* parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* l = new QLabel(text, parent);
    l->setFont(t.font(LauncherTheme::Body));
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    return l;
}
}  // namespace

FirstRunFlow::FirstRunFlow(LauncherApp* app, DetailsSheet* details, QWidget* parent) : QWidget(parent), app_(app), detailsSheet_(details) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(LauncherTheme::S6, LauncherTheme::S5, LauncherTheme::S6, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S3);
    step_ = new QLabel(this);
    step_->setFont(t.font(LauncherTheme::Caption));
    step_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextFaint)));
    col->addWidget(step_);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildWhereIsTheGame());
    stack_->addWidget(buildWhereItLives());
    stack_->addWidget(buildWhichComputer());
    stack_->addWidget(buildPreparing());
    stack_->addWidget(buildReady());
    col->addWidget(stack_, 1);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(LauncherTheme::S2);
    back_ = new FlatButton(QStringLiteral("Back"), FlatButton::Quiet, this);
    cancel_ = new FlatButton(QStringLiteral("Stop"), FlatButton::Secondary, this);
    later_ = new FlatButton(QStringLiteral("Not now"), FlatButton::Quiet, this);
    later_->setAccessibleName(QStringLiteral("Leave the first run for now"));
    next_ = new FlatButton(QStringLiteral("Next"), FlatButton::Primary, this);
    next_->setLarge(true);
    buttons->addWidget(back_);
    buttons->addWidget(cancel_);
    buttons->addStretch(1);
    buttons->addWidget(later_);
    buttons->addWidget(next_);
    col->addLayout(buttons);
    connect(back_, &QAbstractButton::clicked, this, &FirstRunFlow::back);
    connect(next_, &QAbstractButton::clicked, this, &FirstRunFlow::next);
    connect(cancel_, &QAbstractButton::clicked, this, &FirstRunFlow::cancelPreparing);
    connect(later_, &QAbstractButton::clicked, this, [this] { emit completed(); });
    connect(discs_, &DiscList::selectionChanged, this, [this](const QString&) { refreshButtons(); });
    connect(app_, &LauncherApp::foldersChanged, this, [this] { refreshFolderPage(); refreshButtons(); });
    connect(app_->preparer(), &PrepareRunner::progress, this, [this](double fraction, qint64 eta, const QString& phase) {
        etaSeconds_ = eta;
        progress_->setText(phase);
        progress_->setProgress(int(fraction * 1000), 1000);
        progress_->setCaption(QStringLiteral("%1% · %2").arg(int(fraction * 100)).arg(etaText(eta).isEmpty() ? QStringLiteral("time left not known yet") : etaText(eta)));
    });
    connect(app_->preparer(), &PrepareRunner::failed, this, [this](const QString& text, const QString& log) {
        lastLog_ = log;
        setError(text);
        refreshButtons();
    });
    connect(app_->preparer(), &PrepareRunner::finished, this, [this](const QString&) {
        progress_->setProgress(1000, 1000);
        progress_->setCaption(QStringLiteral("Done"));
        showPage(Ready);
    });
    showPage(WhereIsTheGame);
}

QWidget* FirstRunFlow::buildWhereIsTheGame() {
    auto* page = new QWidget();
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QString::fromLatin1(kTitles[0]), QString::fromLatin1(kDescriptions[0]), page));
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    discs_ = new DiscList(app_, true);
    discs_->setObjectName(QStringLiteral("first_run_discs"));
    scroll->setWidget(discs_);
    col->addWidget(scroll, 1);
    return page;
}

QWidget* FirstRunFlow::buildWhereItLives() {
    auto* page = new QWidget();
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QString::fromLatin1(kTitles[1]), QString::fromLatin1(kDescriptions[1]), page));
    folder_ = new FolderRow(QStringLiteral("Game folder"), page);
    folder_->setObjectName(QStringLiteral("first_run_folder"));
    connect(folder_, &FolderRow::changeRequested, this, [this] {
        const QString start = app_->gameFolder()->chosen() ? QFileInfo(app_->gameFolder()->path()).absolutePath() : app_->gameFolder()->defaultParent();
        const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose where the game should live"), start);
        if (!picked.isEmpty()) app_->chooseGameFolder(picked);
    });
    col->addWidget(folder_);
    auto* previewTitle = new QLabel(QStringLiteral("What goes in it"), page);
    previewTitle->setFont(LauncherTheme::instance().font(LauncherTheme::Label));
    col->addSpacing(LauncherTheme::S2);
    col->addWidget(previewTitle);
    layoutPreview_ = mutedLabel(QString(), page);
    layoutPreview_->setFont(LauncherTheme::instance().font(LauncherTheme::Mono));
    col->addWidget(layoutPreview_);
    portableNote_ = mutedLabel(app_->manifest()->portableNote(), page);
    portableNote_->setVisible(!app_->manifest()->portableNote().isEmpty());
    col->addSpacing(LauncherTheme::S2);
    col->addWidget(portableNote_);
    col->addStretch(1);
    return page;
}

QWidget* FirstRunFlow::buildWhichComputer() {
    auto* page = new QWidget();
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QString::fromLatin1(kTitles[2]), QString::fromLatin1(kDescriptions[2]), page));
    targets_ = new SegmentedControl(page);
    targets_->setObjectName(QStringLiteral("first_run_targets"));
    QStringList labels;
    for (const TargetSpec& t : app_->manifest()->targets()) { labels << t.label; targetIds_ << t.id; }
    targets_->setItems(labels, targetIds_);
    targets_->setAccessibleName(QStringLiteral("Which computer"));
    connect(targets_, &SegmentedControl::valueChanged, this, [this](const QString&) { refreshTargetPage(); refreshButtons(); });
    col->addWidget(targets_, 0, Qt::AlignLeft);
    targetNote_ = mutedLabel(QString(), page);
    targetNote_->setMaximumWidth(640);
    col->addWidget(targetNote_);
    col->addStretch(1);
    return page;
}

QWidget* FirstRunFlow::buildPreparing() {
    auto* page = new QWidget();
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    col->addWidget(new PageHeader(QString::fromLatin1(kTitles[3]), QString::fromLatin1(kDescriptions[3]), page));
    prepareIntro_ = mutedLabel(QString(), page);
    col->addWidget(prepareIntro_);
    col->addSpacing(LauncherTheme::S3);
    progress_ = new ProgressPanel(page);
    progress_->setObjectName(QStringLiteral("prepare_progress"));
    progress_->setMaximumWidth(720);
    col->addWidget(progress_);
    error_ = new QLabel(page);
    error_->setFont(LauncherTheme::instance().font(LauncherTheme::Body));
    error_->setWordWrap(true);
    error_->setMaximumWidth(720);
    error_->setObjectName(QStringLiteral("prepare_error"));
    col->addWidget(error_);
    auto* row = new QHBoxLayout();
    row->setSpacing(LauncherTheme::S2);
    retry_ = new FlatButton(QStringLiteral("Try again"), FlatButton::Primary, page);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet, page);
    row->addWidget(retry_);
    row->addWidget(details_);
    row->addStretch(1);
    col->addLayout(row);
    col->addStretch(1);
    connect(retry_, &QAbstractButton::clicked, this, &FirstRunFlow::startPreparing);
    connect(details_, &QAbstractButton::clicked, this, [this] {
        const QString log = app_->preparer()->logPath();
        if (!log.isEmpty() && QFileInfo::exists(log)) detailsSheet_->showLogFile(QStringLiteral("Details"), log);
        else detailsSheet_->showLog(QStringLiteral("Details"), lastLog_.isEmpty() ? QStringLiteral("Nothing has been written yet.") : lastLog_);
    });
    return page;
}

QWidget* FirstRunFlow::buildReady() {
    auto* page = new QWidget();
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S3);
    col->addStretch(2);
    readyTitle_ = new QLabel(QString::fromLatin1(kTitles[4]), page);
    readyTitle_->setFont(LauncherTheme::instance().font(LauncherTheme::Display));
    readyTitle_->setAlignment(Qt::AlignCenter);
    col->addWidget(readyTitle_);
    readyBody_ = mutedLabel(QString(), page);
    readyBody_->setAlignment(Qt::AlignCenter);
    readyBody_->setMaximumWidth(640);
    col->addWidget(readyBody_, 0, Qt::AlignHCenter);
    col->addSpacing(LauncherTheme::S3);
    play_ = new FlatButton(QStringLiteral("Play"), FlatButton::Primary, page);
    play_->setLarge(true);
    play_->setMinimumWidth(200);
    play_->setObjectName(QStringLiteral("first_run_play"));
    col->addWidget(play_, 0, Qt::AlignHCenter);
    openFolder_ = new FlatButton(QStringLiteral("Open the game folder"), FlatButton::Quiet, page);
    col->addWidget(openFolder_, 0, Qt::AlignHCenter);
    col->addStretch(3);
    connect(play_, &QAbstractButton::clicked, this, &FirstRunFlow::play);
    connect(openFolder_, &QAbstractButton::clicked, this, [this] { FolderRow::revealInFileManager(app_->gameFolder()->path()); });
    return page;
}

FirstRunFlow::Page FirstRunFlow::currentPage() const { return Page(stack_->currentIndex()); }

void FirstRunFlow::open(Page page) {
    error_->clear();
    errorText_.clear();
    progress_->clear();
    showPage(page);
}

void FirstRunFlow::showPage(Page page) {
    stack_->setCurrentIndex(int(page));
    step_->setText(QStringLiteral("Step %1 of %2").arg(int(page) + 1).arg(kPages));
    if (page == WhereItLives) refreshFolderPage();
    if (page == WhichComputer) refreshTargetPage();
    if (page == Ready) refreshReadyPage();
    refreshButtons();
}

void FirstRunFlow::refreshFolderPage() {
    GameFolder* f = app_->gameFolder();
    if (suggestedFolder_.isEmpty()) suggestedFolder_ = f->folderFor(f->defaultParent());
    const QString path = f->chosen() ? f->path() : suggestedFolder_;
    folder_->setPath(path);
    const TargetSpec* t = app_->manifest()->target(app_->hostTarget());
    QStringList lines;
    lines << QStringLiteral("%1/").arg(AppData::shortPath(path));
    lines << QStringLiteral("    %1 the game").arg((t ? t->binary : app_->manifest()->binaryName()).leftJustified(20));
    lines << QStringLiteral("    %1 its options").arg(app_->manifest()->configFileName().leftJustified(20));
    for (const SubfolderSpec& s : app_->manifest()->subfolders()) lines << QStringLiteral("    %1 %2").arg((s.defaultName + "/").leftJustified(20), s.label.toLower());
    layoutPreview_->setText(lines.join('\n'));
}

QString FirstRunFlow::selectedTarget() const {
    const QString v = targets_->currentValue();
    return v.isEmpty() ? app_->hostTarget() : v;
}

void FirstRunFlow::refreshTargetPage() {
    const QString host = app_->hostTarget();
    if (targets_->currentIndex() < 0) targets_->setCurrentValue(host);
    const QString id = selectedTarget();
    const TargetSpec* t = app_->manifest()->target(id);
    const QString label = t ? t->label : id;
    QString why;
    const bool can = app_->preparer()->available(id, &why);
    QString note;
    if (id == host) note = QStringLiteral("This computer (%1). The game is prepared here and plays here.").arg(label);
    else note = QStringLiteral("Another computer (%1). The game's files for it are written into the game folder; copy the folder to that computer and "
                               "open the launcher there to finish preparing it. It does not play on this one.").arg(label);
    if (!can && !why.isEmpty()) note += QStringLiteral("\n\n%1").arg(why);
    targetNote_->setText(note);
    targetNote_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(can ? LauncherTheme::TextMuted : LauncherTheme::Warn)));
}

void FirstRunFlow::refreshReadyPage() {
    const QString folder = AppData::shortPath(app_->gameFolder()->path());
    const bool host = selectedTarget() == app_->hostTarget();
    readyTitle_->setText(host ? QStringLiteral("Ready to play") : QStringLiteral("Ready for the other computer"));
    readyBody_->setText(host ? QStringLiteral("%1 is a normal program in %2. Start it from here, or from that folder, whenever you like.").arg(app_->manifest()->name(), folder)
                             : QStringLiteral("The files are in %1. Copy that folder to the other computer and open the launcher there to finish.").arg(folder));
    play_->setVisible(host);
}

void FirstRunFlow::refreshButtons() {
    const Page page = currentPage();
    const bool preparing = app_->preparing();
    back_->setVisible(page != WhereIsTheGame && page != Ready && !preparing);
    cancel_->setVisible(page == Preparing && preparing);
    later_->setVisible(page == WhereIsTheGame || page == WhereItLives || page == WhichComputer);
    later_->setText(page == WhereIsTheGame ? QStringLiteral("Not now") : QStringLiteral("Later"));
    const bool failed = !errorText_.isEmpty();
    retry_->setVisible(page == Preparing && failed && !preparing);
    details_->setVisible(page == Preparing && (failed || preparing || !app_->preparer()->logPath().isEmpty()));
    error_->setVisible(page == Preparing && failed);
    progress_->setVisible(page == Preparing && !failed);
    switch (page) {
    case WhereIsTheGame:
        next_->setText(QStringLiteral("Next"));
        next_->setEnabled(!discs_->selectedDisc().isEmpty());
        next_->setVisible(true);
        break;
    case WhereItLives:
        next_->setText(QStringLiteral("Next"));
        next_->setEnabled(true);
        next_->setVisible(true);
        break;
    case WhichComputer: {
        QString why;
        next_->setText(QStringLiteral("Prepare"));
        next_->setEnabled(app_->preparer()->available(selectedTarget(), &why));
        next_->setVisible(true);
        break;
    }
    case Preparing:
        next_->setVisible(false);
        break;
    case Ready:
        next_->setText(QStringLiteral("Done"));
        next_->setEnabled(true);
        next_->setVisible(true);
        break;
    }
}

void FirstRunFlow::next() {
    switch (currentPage()) {
    case WhereIsTheGame:
        if (discs_->selectedDisc().isEmpty()) return;
        app_->setCurrentDisc(discs_->selectedDisc());
        showPage(WhereItLives);
        break;
    case WhereItLives:
        if (!app_->gameFolder()->chosen()) app_->chooseGameFolder(suggestedFolder_);
        showPage(WhichComputer);
        break;
    case WhichComputer:
        startPreparing();
        break;
    case Preparing:
        break;
    case Ready:
        emit completed();
        break;
    }
}

void FirstRunFlow::back() {
    const int i = stack_->currentIndex();
    if (i > 0 && !app_->preparing()) showPage(Page(i - 1));
}

void FirstRunFlow::setError(const QString& text) {
    errorText_ = text;
    error_->setText(text);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::Error)));
}

QString FirstRunFlow::errorText() const { return errorText_; }

void FirstRunFlow::startPreparing() {
    errorText_.clear();
    error_->clear();
    showPage(Preparing);
    const QString disc = discs_->selectedDisc().isEmpty() ? app_->currentDisc() : discs_->selectedDisc();
    const QString estimate = app_->preparer()->estimateText(selectedTarget());
    prepareIntro_->setText(estimate.isEmpty() ? QStringLiteral("Disc: %1").arg(QFileInfo(disc).fileName())
                                              : QStringLiteral("Disc: %1 · usually %2").arg(QFileInfo(disc).fileName(), estimate));
    progress_->setText(QStringLiteral("Starting"));
    progress_->setIndeterminate(true);
    progress_->setCaption(estimate.isEmpty() ? QStringLiteral("time left not known yet") : QStringLiteral("%1 in all").arg(estimate));
    QString err;
    if (!app_->prepare(disc, selectedTarget(), &err)) setError(err);
    refreshButtons();
}

void FirstRunFlow::cancelPreparing() {
    app_->preparer()->cancel();
    refreshButtons();
}

void FirstRunFlow::play() {
    QString err;
    if (!app_->play(app_->currentDisc(), &err)) {
        readyBody_->setText(err);
        readyBody_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::Error)));
        return;
    }
    emit completed();
}

}  // namespace ps2r::launcher
