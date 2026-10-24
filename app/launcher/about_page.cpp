// app/launcher/about_page.cpp -- the About page.
#include "about_page.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "app_data.h"
#include "details_sheet.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_folder.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "sheet_overlay.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
qint64 folderBytes(const QString& path) {
    qint64 total = 0;
    for (const QFileInfo& fi : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        total += fi.isDir() ? folderBytes(fi.absoluteFilePath()) : fi.size();
    }
    return total;
}
QString bytesText(qint64 bytes) {
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024ll * 1024 * 1024) return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1));
}
}  // namespace

QString AboutPage::shippedFile(const QString& name) {
    for (const QString& p : {QApplication::applicationDirPath() + "/../Resources/" + name, QApplication::applicationDirPath() + "/" + name,
                             QApplication::applicationDirPath() + "/../" + name}) {
        if (QFile::exists(p)) return QDir::cleanPath(p);
    }
    return QString();
}

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
    auto* product = new QLabel(app->manifest()->name(), inner);
    product->setFont(t.font(LauncherTheme::Title));
    product->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(product);
    auto* desc = new QLabel(app->manifest()->description(), inner);
    desc->setFont(t.font(LauncherTheme::Body));
    desc->setWordWrap(true);
    desc->setMaximumWidth(600);
    desc->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    col->addWidget(desc);
    col->addSpacing(LauncherTheme::S2);
    versions_ = new QLabel(inner);
    versions_->setFont(t.font(LauncherTheme::Body));
    versions_->setWordWrap(true);
    versions_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    versions_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(versions_);

    col->addSpacing(LauncherTheme::S3);
    auto* unsupportedTitle = new QLabel(QStringLiteral("What is not supported"), inner);
    unsupportedTitle->setFont(t.font(LauncherTheme::Label));
    unsupportedTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(unsupportedTitle);
    unsupported_ = new QLabel(inner);
    unsupported_->setObjectName(QStringLiteral("about_unsupported"));
    unsupported_->setFont(t.font(LauncherTheme::Body));
    unsupported_->setWordWrap(true);
    unsupported_->setMaximumWidth(600);
    unsupported_->setTextFormat(Qt::RichText);
    unsupported_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    QString html;
    for (const UnsupportedItem& u : app->manifest()->unsupported()) {
        html += QStringLiteral("<p style='margin:0 0 6px 0'><span style='color:%1'><b>%2</b></span><br>%3</p>").arg(t.css(LauncherTheme::Text), u.label.toHtmlEscaped(), u.reason.toHtmlEscaped());
    }
    if (html.isEmpty()) html = QStringLiteral("Everything the game description names is supported.");
    unsupported_->setText(html);
    col->addWidget(unsupported_);

    col->addSpacing(LauncherTheme::S3);
    auto* whereTitle = new QLabel(QStringLiteral("Where the files are"), inner);
    whereTitle->setFont(t.font(LauncherTheme::Label));
    whereTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(whereTitle);
    gameFolderRow_ = linkRow(QStringLiteral("Game folder"), app->gameFolder()->chosen() ? app->gameFolder()->path() : QStringLiteral("not chosen yet"), inner);
    col->addWidget(gameFolderRow_);
    col->addWidget(linkRow(QStringLiteral("Launcher data"), app->appData()->root(), inner));
    col->addWidget(linkRow(QStringLiteral("Logs"), app->appData()->logsDir(), inner));

    col->addSpacing(LauncherTheme::S3);
    auto* licTitle = new QLabel(QStringLiteral("Licences"), inner);
    licTitle->setFont(t.font(LauncherTheme::Label));
    licTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(licTitle);
    auto* lic = new QHBoxLayout();
    lic->setSpacing(LauncherTheme::S2);
    for (const LicenceLink& l : app->manifest()->licences()) {
        auto* b = new FlatButton(l.label, FlatButton::Quiet, inner);
        connect(b, &QAbstractButton::clicked, this, [this, l] {
            const QString p = shippedFile(l.file);
            if (!p.isEmpty()) details_->showLogFile(l.label, p);
            else details_->showLog(l.label, QStringLiteral("%1 is not beside this application.").arg(l.file));
        });
        lic->addWidget(b);
    }
    lic->addStretch(1);
    col->addLayout(lic);

    col->addSpacing(LauncherTheme::S4);
    auto* removeRow = new QHBoxLayout();
    auto* removeText = new QLabel(QStringLiteral("Forget the launcher's own data (the list of discs, the window, the folder choice)"), inner);
    removeText->setFont(t.font(LauncherTheme::Body));
    removeText->setWordWrap(true);
    removeText->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    remove_ = new FlatButton(QStringLiteral("Forget…"), FlatButton::Secondary, inner);
    removeRow->addWidget(removeText, 1);
    removeRow->addWidget(remove_);
    col->addLayout(removeRow);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(remove_, &QAbstractButton::clicked, this, &AboutPage::openRemoveSheet);
    connect(app_, &LauncherApp::stateChanged, this, &AboutPage::refresh);
    connect(app_, &LauncherApp::foldersChanged, this, &AboutPage::refresh);
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
    l->setMinimumWidth(112);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    auto* p = new QLabel(AppData::shortPath(path), w);
    p->setObjectName(QStringLiteral("about_path"));
    p->setFont(t.font(LauncherTheme::Body));
    p->setTextInteractionFlags(Qt::TextSelectableByMouse);
    p->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    auto* open = new FlatButton(QStringLiteral("Open"), FlatButton::Quiet, w);
    open->setObjectName(QStringLiteral("about_open"));
    open->setAccessibleName(QStringLiteral("Open %1").arg(label));
    open->setEnabled(QFileInfo::exists(path));
    connect(open, &QAbstractButton::clicked, this, [path] { FolderRow::revealInFileManager(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath()); });
    row->addWidget(l);
    row->addWidget(p, 1);
    row->addWidget(open);
    return w;
}

QString AboutPage::unsupportedText() const { return unsupported_->text(); }

void AboutPage::refresh() {
    QString launcher = QStringLiteral("Launcher %1").arg(app_->version());
    if (!app_->engineCommit().isEmpty()) launcher += QStringLiteral(" (engine %1)").arg(app_->engineCommit());
    launcher += QStringLiteral(" · Qt %1").arg(QString::fromLatin1(qVersion()));
    const PreparedRecord r = app_->gameFolder()->prepared();
    QString game = r.ok ? QStringLiteral("Game prepared %1 by launcher %2").arg(r.when.toString(QStringLiteral("yyyy-MM-dd")), r.launcherVersion) : QStringLiteral("Game not prepared yet");
    versions_->setText(launcher + '\n' + game);
    if (auto* path = gameFolderRow_->findChild<QLabel*>(QStringLiteral("about_path"))) path->setText(app_->gameFolder()->chosen() ? AppData::shortPath(app_->gameFolder()->path()) : QStringLiteral("not chosen yet"));
    if (auto* open = gameFolderRow_->findChild<FlatButton*>(QStringLiteral("about_open"))) open->setEnabled(app_->gameFolder()->chosen() && QFileInfo::exists(app_->gameFolder()->path()));
}

void AboutPage::openRemoveSheet() {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* content = new QWidget();
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    const QString root = app_->appData()->root();
    auto* text = new QLabel(QStringLiteral("This deletes the launcher's own folder %1 (%2): the list of discs, the window, the folder choice and the logs. "
                                           "The game folder, its saves and your disc images are not touched. The launcher then closes and starts over next time.")
                                .arg(AppData::shortPath(root), bytesText(folderBytes(root))), content);
    text->setFont(t.font(LauncherTheme::Body));
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    col->addWidget(text);
    auto* sheet = new SheetOverlay(QStringLiteral("Forget the launcher's data"), content, window());
    auto* cancel = new FlatButton(QStringLiteral("Cancel"), FlatButton::Secondary);
    auto* remove = new FlatButton(QStringLiteral("Forget"), FlatButton::Primary);
    sheet->addButton(cancel);
    sheet->addButton(remove);
    sheet->setCancelButton(cancel);
    connect(cancel, &QAbstractButton::clicked, sheet, [sheet] { sheet->close(); sheet->deleteLater(); });
    connect(remove, &QAbstractButton::clicked, this, [root, sheet] {
        if (root.startsWith(AppData::homePath()) && root != AppData::homePath()) QDir(root).removeRecursively();
        sheet->close();
        sheet->deleteLater();
        qApp->quit();
    });
    sheet->open();
}

}  // namespace ps2r::launcher
