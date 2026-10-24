// app/launcher/settings_page.cpp -- the Settings page.
#include "settings_page.h"
#include <QFileDialog>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "app_data.h"
#include "folder_row.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "settings_row.h"
#include "title_manifest.h"
namespace ps2r::launcher {

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
    col->addWidget(new PageHeader(QStringLiteral("Settings"), QStringLiteral("The same settings the game shows under Options; a change here is a change there."), inner));
    for (const SettingsGroupSpec& g : app->manifest()->settingsGroups()) {
        auto* title = new QLabel(g.title, inner);
        title->setFont(t.font(LauncherTheme::Title));
        title->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
        col->addSpacing(LauncherTheme::S3);
        col->addWidget(title);
        for (const SettingsRowSpec& spec : g.rows) {
            auto* row = new SettingsRow(spec, inner);
            connect(row, &SettingsRow::valueChanged, this, [this](const QString& key, const QString& value) {
                QString err;
                app_->setSettingValue(key, value, &err);
                applyDependencies();
            });
            rows_ << row;
            col->addWidget(row);
        }
    }
    for (const FolderSpec& f : app->manifest()->folders()) {
        if (f.id != "game_output") continue;
        auto* title = new QLabel(QStringLiteral("Installed game"), inner);
        title->setFont(t.font(LauncherTheme::Title));
        title->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
        col->addSpacing(LauncherTheme::S3);
        col->addWidget(title);
        gameFolder_ = new FolderRow(f.label, inner);
        gameFolder_->setObjectName(QStringLiteral("folder_game_output_settings"));
        connect(gameFolder_, &FolderRow::changeRequested, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose where to install the game"), app_->folder("game_output"));
            if (!dir.isEmpty()) app_->chooseGameFolder(dir);
        });
        connect(app_, &LauncherApp::foldersChanged, this, [this] { gameFolder_->setPath(app_->folder("game_output")); });
        gameFolder_->setPath(app_->folder("game_output"));
        col->addWidget(gameFolder_);
    }
    for (const FolderSpec& f : app->manifest()->folders()) {
        if (f.id != "memory_cards") continue;
        auto* title = new QLabel(QStringLiteral("Saves"), inner);
        title->setFont(t.font(LauncherTheme::Title));
        title->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
        col->addSpacing(LauncherTheme::S3);
        col->addWidget(title);
        memoryCards_ = new FolderRow(f.label, inner);
        memoryCards_->setObjectName(QStringLiteral("folder_memory_cards_settings"));
        connect(memoryCards_, &FolderRow::changeRequested, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose the memory-card folder"), app_->folder("memory_cards"));
            if (!dir.isEmpty()) app_->setFolder("memory_cards", dir);
        });
        connect(app_, &LauncherApp::foldersChanged, this, [this] { memoryCards_->setPath(app_->folder("memory_cards")); });
        memoryCards_->setPath(app_->folder("memory_cards"));
        col->addWidget(memoryCards_);
    }
    auto* note = new QLabel(inner);
    note->setFont(t.font(LauncherTheme::Caption));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    const QString n = app->manifest()->settingsNote();
    note->setText(n.isEmpty() ? QStringLiteral("Written to %1 as the game reads it.").arg(AppData::shortPath(app->settingsFilePath())) : n);
    col->addSpacing(LauncherTheme::S3);
    col->addWidget(note);
    col->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    reload();
}

SettingsRow* SettingsPage::row(const QString& key) const {
    for (SettingsRow* r : rows_) if (r->spec().key == key) return r;
    return nullptr;
}

void SettingsPage::reload() {
    for (SettingsRow* r : rows_) r->setValue(app_->settingValue(r->spec().key));
    applyDependencies();
}

void SettingsPage::applyDependencies() {
    // the game's rule: UNCAPPED forces vertical sync off for the session, and the vsync row says so
    SettingsRow* rate = row("video.frame_rate");
    SettingsRow* vsync = row("video.vsync");
    if (!rate || !vsync) return;
    const bool uncapped = rate->value() == "uncapped";
    vsync->setLocked(uncapped);
    vsync->setHelpOverride(uncapped ? QStringLiteral("Off while the frame rate is uncapped.") : QString());
}

void SettingsPage::showEvent(QShowEvent* event) {
    reload();
    QWidget::showEvent(event);
}

}  // namespace ps2r::launcher
