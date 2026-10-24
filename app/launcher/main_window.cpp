// app/launcher/main_window.cpp -- the window.
#include "main_window.h"
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QStackedWidget>
#include "about_page.h"
#include "app_data.h"
#include "details_sheet.h"
#include "first_run_flow.h"
#include "folder_row.h"
#include "game_folder.h"
#include "game_process.h"
#include "games_page.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "save_states_page.h"
#include "settings_page.h"
#include "sidebar.h"
#include "title_manifest.h"
namespace ps2r::launcher {

MainWindow::MainWindow(LauncherApp* app, QWidget* parent) : QMainWindow(parent), app_(app) {
    setWindowTitle(app->manifest()->name());
    setMinimumSize(1024, 640);
    auto* central = new QWidget(this);
    auto* centralCol = new QHBoxLayout(central);
    centralCol->setContentsMargins(0, 0, 0, 0);
    root_ = new QStackedWidget(central);
    centralCol->addWidget(root_);
    details_ = new DetailsSheet(central);

    // the pages: the sidebar and the stack
    pagesArea_ = new QWidget(root_);
    auto* row = new QHBoxLayout(pagesArea_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    QString version = QStringLiteral("Launcher %1").arg(app->version());
    if (!app->engineCommit().isEmpty()) version += QStringLiteral(" · engine %1").arg(app->engineCommit().left(7));
    sidebar_ = new Sidebar(app->manifest()->name(), version,
                           {QStringLiteral("Games"), QStringLiteral("Save States"), QStringLiteral("Settings"), QStringLiteral("About")}, pagesArea_);
    sidebar_->setFooter(app->gameFolder()->chosen() ? app->gameFolder()->path() : app->appData()->root());
    content_ = new QWidget(pagesArea_);
    stack_ = new QStackedWidget(content_);
    auto* contentCol = new QHBoxLayout(content_);
    contentCol->setContentsMargins(0, 0, 0, 0);
    contentCol->addWidget(stack_);
    games_ = new GamesPage(app, details_, stack_);
    saves_ = new SaveStatesPage(app, stack_);
    settings_ = new SettingsPage(app, stack_);
    about_ = new AboutPage(app, details_, stack_);
    stack_->addWidget(games_);
    stack_->addWidget(saves_);
    stack_->addWidget(settings_);
    stack_->addWidget(about_);
    row->addWidget(sidebar_);
    row->addWidget(content_, 1);

    firstRun_ = new FirstRunFlow(app, details_, root_);
    root_->addWidget(firstRun_);
    root_->addWidget(pagesArea_);
    setCentralWidget(central);

    connect(sidebar_, &Sidebar::selected, this, &MainWindow::showPage);
    connect(sidebar_, &Sidebar::openRequested, this, [this] {
        FolderRow::revealInFileManager(app_->gameFolder()->chosen() ? app_->gameFolder()->path() : app_->appData()->root());
    });
    connect(app_, &LauncherApp::foldersChanged, this, [this] {
        sidebar_->setFooter(app_->gameFolder()->chosen() ? app_->gameFolder()->path() : app_->appData()->root());
    });
    connect(firstRun_, &FirstRunFlow::completed, this, &MainWindow::showPages);
    connect(games_, &GamesPage::prepareRequested, this, [this](const QString&) { showFirstRun(FirstRunFlow::WhichComputer); });
    connect(games_, &GamesPage::folderRequested, this, [this] { showFirstRun(FirstRunFlow::WhereItLives); });
    connect(games_, &GamesPage::otherComputerRequested, this, [this] { showFirstRun(FirstRunFlow::WhichComputer); });
    connect(settings_, &SettingsPage::folderRequested, this, [this] { showFirstRun(FirstRunFlow::WhereItLives); });
    for (int i = 0; i < 4; ++i) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        connect(a, &QAction::triggered, this, [this, i] { if (!firstRunShowing()) showPage(i); });
        addAction(a);
    }
    auto* shot = new QAction(QStringLiteral("Save a screenshot"), this);
    shot->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(shot, &QAction::triggered, this, [this] { screenshot(); });
    addAction(shot);
    connect(&LauncherTheme::instance(), &LauncherTheme::changed, this, [] { LauncherStyle::apply(); });
    const QJsonObject win = app->appData()->value("window").toObject();
    const int w = qMax(1024, win.value("width").toInt(1120)), h = qMax(640, win.value("height").toInt(720));
    resize(w, h);
    showPage(qBound(0, win.value("page").toInt(0), 3));
    root_->setCurrentWidget(pagesArea_);
}

MainWindow::~MainWindow() = default;

void MainWindow::showPage(int index) {
    stack_->setCurrentIndex(index);
    sidebar_->setCurrent(index);
    if (index == SettingsIndex) settings_->reload();
    if (index == SaveStatesIndex) saves_->refresh();
}

int MainWindow::currentPage() const { return stack_->currentIndex(); }
bool MainWindow::firstRunShowing() const { return root_->currentWidget() == firstRun_; }

void MainWindow::showFirstRun(int page) {
    firstRun_->open(FirstRunFlow::Page(page));
    root_->setCurrentWidget(firstRun_);
}

void MainWindow::showPages() {
    root_->setCurrentWidget(pagesArea_);
    showPage(currentPage());
}

bool MainWindow::startFirstRunIfNeeded() {
    if (app_->setupComplete()) return false;
    showFirstRun(FirstRunFlow::WhereIsTheGame);
    return true;
}

QString MainWindow::screenshot(const QString& path) {
    QString out = path;
    if (out.isEmpty()) {
        QDir().mkpath(app_->appData()->logsDir());
        out = app_->appData()->logsDir() + "/screenshot-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
    }
    QPixmap pm = grab();
    pm.save(out, "PNG");
    return out;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QJsonObject win;
    win["width"] = width();
    win["height"] = height();
    win["page"] = stack_->currentIndex();
    app_->appData()->setValue("window", win);
    app_->appData()->save();
    if (app_->game()->running()) app_->game()->stop();
    QMainWindow::closeEvent(event);
}

}  // namespace ps2r::launcher
