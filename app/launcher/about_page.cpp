// app/launcher/about_page.cpp -- the About page.
#include "about_page.h"
#include <QApplication>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "app_data.h"
#include "details_sheet.h"
#include "flat_button.h"
#include "folder_row.h"
#include "installer.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "sheet_overlay.h"
#include "switch_control.h"
#include "title_manifest.h"
namespace ps2r::launcher {

AboutPage::AboutPage(LauncherApp* app, DetailsSheet* details, QWidget* parent) : QWidget(parent), app_(app), details_(details) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    scroll->setWidget(inner);
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(LauncherTheme::S5, 0, LauncherTheme::S5, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(new PageHeader(QStringLiteral("About"), QString(), inner));
    auto* product = new QLabel(app->manifest()->product(), inner);
    product->setFont(t.font(LauncherTheme::Title));
    product->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(product);
    auto* desc = new QLabel(app->manifest()->description(), inner);
    desc->setFont(t.font(LauncherTheme::Body));
    desc->setWordWrap(true);
    desc->setMaximumWidth(560);
    desc->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    col->addWidget(desc);
    col->addSpacing(LauncherTheme::S3);
    versions_ = new QLabel(inner);
    versions_->setFont(t.font(LauncherTheme::Body));
    versions_->setWordWrap(true);
    versions_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    versions_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(versions_);
    col->addSpacing(LauncherTheme::S3);
    col->addWidget(linkRow(QStringLiteral("App data"), app->appData()->root(), inner));
    col->addWidget(linkRow(QStringLiteral("Settings"), app->settingsFilePath(), inner));
    col->addWidget(linkRow(QStringLiteral("Logs"), app->appData()->logsDir(), inner));
    col->addSpacing(LauncherTheme::S3);
    auto* licTitle = new QLabel(QStringLiteral("Licences"), inner);
    licTitle->setFont(t.font(LauncherTheme::Label));
    licTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(licTitle);
    auto* lic = new QHBoxLayout();
    lic->setSpacing(LauncherTheme::S2);
    auto* notices = new FlatButton(QStringLiteral("Third-party notices"), FlatButton::Quiet, inner);
    connect(notices, &QAbstractButton::clicked, this, [this] {
        for (const QString& p : {QApplication::applicationDirPath() + "/../Resources/THIRD_PARTY_NOTICES.md", QApplication::applicationDirPath() + "/THIRD_PARTY_NOTICES.md",
                                 app_->installer()->sourceDir() + "/THIRD_PARTY_NOTICES.md"}) {
            if (QFile::exists(p)) { details_->showLogFile(QStringLiteral("Third-party notices"), p); return; }
        }
        details_->showLog(QStringLiteral("Third-party notices"), QStringLiteral("The notices file is not beside this application."));
    });
    lic->addWidget(notices);
    for (const LicenceLink& l : app->manifest()->licences()) {
        auto* b = new FlatButton(l.label, FlatButton::Quiet, inner);
        connect(b, &QAbstractButton::clicked, this, [this, l] {
            const QString p = app_->installer()->sourceDir() + "/" + l.path;
            if (QFile::exists(p)) details_->showLogFile(l.label, p);
            else details_->showLog(l.label, QStringLiteral("%1 is part of the game's source, which is installed on the first run.").arg(l.path));
        });
        lic->addWidget(b);
    }
    lic->addStretch(1);
    col->addLayout(lic);
    col->addSpacing(LauncherTheme::S4);
    auto* removeRow = new QHBoxLayout();
    auto* removeText = new QLabel(QStringLiteral("Remove everything this launcher installed"), inner);
    removeText->setFont(t.font(LauncherTheme::Body));
    removeText->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    remove_ = new FlatButton(QStringLiteral("Remove…"), FlatButton::Secondary, inner);
    removeRow->addWidget(removeText, 1);
    removeRow->addWidget(remove_);
    col->addLayout(removeRow);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(remove_, &QAbstractButton::clicked, this, &AboutPage::openRemoveSheet);
    connect(app_, &LauncherApp::stateChanged, this, &AboutPage::refresh);
    refresh();
}

QWidget* AboutPage::linkRow(const QString& label, const QString& path, QWidget* parent) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* w = new QWidget(parent);
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(LauncherTheme::S2);
    auto* l = new QLabel(label, w);
    l->setFont(t.font(LauncherTheme::Label));
    l->setMinimumWidth(96);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    auto* p = new QLabel(AppData::shortPath(path), w);
    p->setFont(t.font(LauncherTheme::Body));
    p->setTextInteractionFlags(Qt::TextSelectableByMouse);
    p->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    auto* open = new FlatButton(QStringLiteral("Open"), FlatButton::Quiet, w);
    open->setAccessibleName(QStringLiteral("Open %1").arg(label));
    connect(open, &QAbstractButton::clicked, this, [path] { FolderRow::revealInFileManager(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath()); });
    row->addWidget(l);
    row->addWidget(p, 1);
    row->addWidget(open);
    return w;
}

void AboutPage::refresh() {
    QStringList lines;
    QString launcher = QStringLiteral("Launcher %1").arg(app_->version());
    if (!app_->engineCommit().isEmpty()) launcher += QStringLiteral(" (engine %1)").arg(app_->engineCommit());
    lines << launcher + QStringLiteral(" · Qt %1").arg(QString::fromLatin1(qVersion()));
    QString src = QStringLiteral("Game source %1").arg(app_->installer()->sourceCommit().isEmpty() ? QStringLiteral("not installed yet") : app_->installer()->sourceCommit().left(7));
    const QString tc = app_->installer()->installed().value("toolchain_detail").toString();
    if (!tc.isEmpty()) src += QStringLiteral(" · pinned toolchain %1").arg(tc);
    lines << src;
    versions_->setText(lines.join('\n'));
}

void AboutPage::openRemoveSheet() {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* content = new QWidget();
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    const qint64 bytes = Installer::folderBytes(app_->appData()->root());
    auto* text = new QLabel(QStringLiteral("This deletes the app-data folder %1 (%2): the tools, the game's source, every build and the launcher's own settings. "
                                           "Your disc images are not touched.").arg(AppData::shortPath(app_->appData()->root()), Installer::bytesText(bytes)), content);
    text->setFont(t.font(LauncherTheme::Body));
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(text);
    auto* gameRow = new QHBoxLayout();
    auto* gameSwitch = new SwitchControl(content);
    gameSwitch->setAccessibleName(QStringLiteral("Also remove the game's settings and memory cards"));
    const QString gameDir = QFileInfo(app_->settingsFilePath()).absolutePath();
    auto* gameText = new QLabel(QStringLiteral("Also remove the game's own settings and memory cards (%1, %2)").arg(AppData::shortPath(gameDir), Installer::bytesText(Installer::folderBytes(gameDir))), content);
    gameText->setFont(t.font(LauncherTheme::Body));
    gameText->setWordWrap(true);
    gameText->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    gameRow->addWidget(gameSwitch);
    gameRow->addWidget(gameText, 1);
    col->addLayout(gameRow);
    auto* sheet = new SheetOverlay(QStringLiteral("Remove"), content, window());
    auto* cancel = new FlatButton(QStringLiteral("Cancel"), FlatButton::Secondary);
    auto* remove = new FlatButton(QStringLiteral("Remove"), FlatButton::Primary);
    sheet->addButton(cancel);
    sheet->addButton(remove);
    sheet->setCancelButton(cancel);
    connect(cancel, &QAbstractButton::clicked, sheet, [sheet] { sheet->close(); sheet->deleteLater(); });
    connect(remove, &QAbstractButton::clicked, this, [this, sheet, gameSwitch, gameDir] {
        const QString root = app_->appData()->root();
        if (gameSwitch->isChecked() && gameDir.startsWith(AppData::homePath()) && gameDir != AppData::homePath()) QDir(gameDir).removeRecursively();
        if (root.startsWith(AppData::homePath()) && root != AppData::homePath()) QDir(root).removeRecursively();
        sheet->close();
        sheet->deleteLater();
        qApp->quit();
    });
    sheet->open();
}

}  // namespace ps2r::launcher
