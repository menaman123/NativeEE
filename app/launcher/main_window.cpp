// app/launcher/main_window.cpp -- the window.
#include "main_window.h"
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QScreen>
#include <QStackedWidget>
#include <QWindow>
#include "about_page.h"
#include "app_data.h"
#include "details_sheet.h"
#include "first_run_sheet.h"
#include "folder_row.h"
#include "game_process.h"
#include "games_page.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "settings_page.h"
#include "sidebar.h"
#include "targets_page.h"
#include "title_manifest.h"
namespace ps2r::launcher {

MainWindow::MainWindow(LauncherApp* app, QWidget* parent) : QMainWindow(parent), app_(app) {
    setWindowTitle(app->manifest()->product());
    setMinimumSize(1024, 640);
    auto* central = new QWidget(this);
    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    QString version = QStringLiteral("Launcher %1").arg(app->version());
    if (!app->engineCommit().isEmpty()) version += QStringLiteral(" · engine %1").arg(app->engineCommit().left(7));
    sidebar_ = new Sidebar(app->manifest()->product(), version, {QStringLiteral("Games"), QStringLiteral("Settings"), QStringLiteral("Targets"), QStringLiteral("About")}, central);
    sidebar_->setFooter(app->appData()->root());
    content_ = new QWidget(central);
    stack_ = new QStackedWidget(content_);
    auto* contentCol = new QHBoxLayout(content_);
    contentCol->setContentsMargins(0, 0, 0, 0);
    contentCol->addWidget(stack_);
    details_ = new DetailsSheet(content_);
    games_ = new GamesPage(app, details_, stack_);
    settings_ = new SettingsPage(app, stack_);
    targets_ = new TargetsPage(app, details_, stack_);
    about_ = new AboutPage(app, details_, stack_);
    stack_->addWidget(games_);
    stack_->addWidget(settings_);
    stack_->addWidget(targets_);
    stack_->addWidget(about_);
    firstRun_ = new FirstRunSheet(app->installer(), details_, content_);
    games_->setFirstRunSheet(firstRun_);
    row->addWidget(sidebar_);
    row->addWidget(content_, 1);
    setCentralWidget(central);
    connect(sidebar_, &Sidebar::selected, this, &MainWindow::showPage);
    connect(sidebar_, &Sidebar::openRequested, this, [this] { FolderRow::revealInFileManager(app_->appData()->root()); });
    for (int i = 0; i < 4; ++i) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        connect(a, &QAction::triggered, this, [this, i] { showPage(i); });
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
}

MainWindow::~MainWindow() = default;

void MainWindow::showPage(int index) {
    stack_->setCurrentIndex(index);
    sidebar_->setCurrent(index);
    if (index == 1) settings_->reload();
}

int MainWindow::currentPage() const { return stack_->currentIndex(); }

void MainWindow::askGameFolderIfNeeded() {
    if (app_->gameFolderChosen()) return;
    const QString name = app_->manifest()->name();
    QMessageBox::information(this, QStringLiteral("Where should %1 go?").arg(name),
                             QStringLiteral("%1 installs %2 as a normal program you can start without the launcher, and plays "
                                            "it with your own disc.\n\nChoose the folder to put it in (any drive): a \"%2\" folder "
                                            "is made there, with a shortcut on your Desktop.").arg(app_->manifest()->product(), name));
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose where to install %1").arg(name), start);
    app_->chooseGameFolder(picked.isEmpty() ? start : picked);
}

bool MainWindow::startFirstRunIfNeeded() {
    askGameFolderIfNeeded();
    if (app_->installer()->complete()) {
        app_->installer()->checkForSourceUpdate();
        return false;
    }
    firstRun_->open();
    app_->install();
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
