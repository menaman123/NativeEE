// app/launcher/settings_page.cpp -- the Settings page.
#include "settings_page.h"
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "app_data.h"
#include "empty_state.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_folder.h"
#include "host_platform.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "settings_row.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
// the row's choices as offered on this machine: a choice naming platforms is kept only when this os is among them
SettingsRowSpec forThisMachine(const SettingsRowSpec& spec) {
    SettingsRowSpec out = spec;
    out.choices.clear();
    for (const SettingsChoice& c : spec.choices) if (c.platforms.isEmpty() || c.platforms.contains(HostPlatform::os())) out.choices << c;
    return out;
}
}  // namespace

SettingsPage::SettingsPage(LauncherApp* app, QWidget* parent) : QWidget(parent), app_(app) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    scroll->setWidget(inner);
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(LauncherTheme::S5, 0, LauncherTheme::S5, LauncherTheme::S5);
    col->setSpacing(LauncherTheme::S1);
    col->addWidget(new PageHeader(QStringLiteral("Settings"), QStringLiteral("The same settings the game shows in its own menu; a change here is a change there."), inner));
    empty_ = new EmptyState(QStringLiteral("Choose where the game lives first"),
                            QStringLiteral("The settings are kept in the game folder, beside the game. Pick the folder and they appear here."),
                            QStringLiteral("Choose a folder…"), QString(), inner);
    empty_->setObjectName(QStringLiteral("settings_empty"));
    connect(empty_, &EmptyState::primaryClicked, this, &SettingsPage::folderRequested);
    col->addWidget(empty_);
    body_ = new QWidget(inner);
    auto* body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(LauncherTheme::S1);
    for (const SettingsGroupSpec& g : app->manifest()->settingsGroups()) {
        auto* title = new QLabel(g.title, body_);
        title->setFont(t.font(LauncherTheme::Title));
        title->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
        body->addSpacing(LauncherTheme::S3);
        body->addWidget(title);
        for (const SettingsRowSpec& spec : g.rows) {
            auto* row = new SettingsRow(forThisMachine(spec), body_);
            connect(row, &SettingsRow::valueChanged, this, [this](const QString& key, const QString& value) {
                QString err;
                app_->setSettingValue(key, value, &err);
                applyLocks();
            });
            rows_ << row;
            body->addWidget(row);
        }
    }
    auto* foldersTitle = new QLabel(QStringLiteral("Folders"), body_);
    foldersTitle->setFont(t.font(LauncherTheme::Title));
    foldersTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    body->addSpacing(LauncherTheme::S3);
    body->addWidget(foldersTitle);
    auto* gameRow = new FolderRow(QStringLiteral("Game folder"), body_);
    gameRow->setObjectName(QStringLiteral("folder_game_settings"));
    gameRow->setProperty("folder_id", QStringLiteral("game"));
    connect(gameRow, &FolderRow::changeRequested, this, &SettingsPage::folderRequested);
    folders_ << gameRow;
    body->addWidget(gameRow);
    // the sub-folders read the options file's own keys (paths.mc_dir / paths.save_dir / paths.user_data): the same
    // store the setup window wrote and the game reads; one without a key is fixed inside the game folder
    for (const SubfolderSpec& s : app->manifest()->subfolders()) {
        auto* row = new FolderRow(s.label, body_);
        row->setObjectName(QStringLiteral("folder_%1_settings").arg(s.id));
        row->setProperty("folder_id", s.id);
        if (!s.help.isEmpty()) row->setToolTip(s.help);
        row->changeButton()->setVisible(!s.key.isEmpty());
        connect(row, &FolderRow::changeRequested, this, [this, id = s.id, label = s.label] {
            const QString current = app_->gameFolder()->subfolder(id);
            const QString start = QDir(current).exists() ? current : app_->gameFolder()->path();
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose where the %1 should be").arg(label.toLower()), start);
            if (!dir.isEmpty()) app_->setSubfolder(id, dir);
        });
        folders_ << row;
        body->addWidget(row);
    }
    auto* note = new QLabel(body_);
    note->setFont(t.font(LauncherTheme::Caption));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    note->setObjectName(QStringLiteral("settings_note"));
    const QString n = app->manifest()->settingsNote();
    note->setText(n.isEmpty() ? QStringLiteral("Written to %1 in the game folder, the file the game reads.").arg(app->manifest()->configFileName()) : n);
    body->addSpacing(LauncherTheme::S3);
    body->addWidget(note);
    col->addWidget(body_);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(app_, &LauncherApp::foldersChanged, this, [this] { refreshFolders(); reload(); });
    refreshFolders();
    reload();
}

SettingsRow* SettingsPage::row(const QString& key) const {
    for (SettingsRow* r : rows_) if (r->spec().key == key) return r;
    return nullptr;
}

FolderRow* SettingsPage::folderRow(const QString& id) const {
    for (FolderRow* r : folders_) if (r->property("folder_id").toString() == id) return r;
    return nullptr;
}

void SettingsPage::refreshFolders() {
    GameFolder* f = app_->gameFolder();
    for (FolderRow* r : folders_) {
        const QString id = r->property("folder_id").toString();
        if (!f->chosen()) { r->setPath(QString()); continue; }
        if (id == QLatin1String("game")) { r->setPath(f->path()); continue; }
        // inside the game folder: the relative form, as on the setup window; elsewhere: the path
        r->setPath(f->subfolder(id), f->subfolderInside(id) ? f->subfolderChoice(id) : QString());
    }
}

void SettingsPage::reload() {
    const bool chosen = app_->gameFolder()->chosen();
    empty_->setVisible(!chosen);
    body_->setVisible(chosen);
    for (SettingsRow* r : rows_) r->setValue(app_->settingValue(r->spec().key));
    applyLocks();
}

void SettingsPage::applyLocks() {
    for (SettingsRow* r : rows_) {
        const SettingsLock& lock = r->spec().lockedBy;
        if (lock.key.isEmpty()) continue;
        SettingsRow* other = row(lock.key);
        const bool locked = other ? other->value() == lock.value : app_->settingValue(lock.key) == lock.value;
        r->setLocked(locked);
        r->setHelpOverride(locked ? lock.help : QString());
    }
}

void SettingsPage::showEvent(QShowEvent* event) {
    reload();
    QWidget::showEvent(event);
}

}  // namespace ps2r::launcher
