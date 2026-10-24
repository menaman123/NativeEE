// app/launcher/setup_page.cpp -- the setup window.
#include "setup_page.h"
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include "app_data.h"
#include "details_sheet.h"
#include "flat_button.h"
#include "folder_row.h"
#include "game_card.h"
#include "game_folder.h"
#include "game_library.h"
#include "host_platform.h"
#include "launcher_app.h"
#include "launcher_theme.h"
#include "page_header.h"
#include "prepare_runner.h"
#include "progress_panel.h"
#include "segmented_control.h"
#include "settings_row.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
QLabel* mutedLabel(const QString& text, QWidget* parent, LauncherTheme::TextStyle style = LauncherTheme::Body) {
    const LauncherTheme& t = LauncherTheme::instance();
    auto* l = new QLabel(text, parent);
    l->setFont(t.font(style));
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    return l;
}
// the row's choices as offered on this machine: a choice naming platforms is kept only when this os is among them
SettingsRowSpec forThisMachine(const SettingsRowSpec& spec) {
    SettingsRowSpec out = spec;
    out.choices.clear();
    for (const SettingsChoice& c : spec.choices) if (c.platforms.isEmpty() || c.platforms.contains(HostPlatform::os())) out.choices << c;
    return out;
}
}  // namespace

QString SetupPage::etaText(qint64 seconds) {
    if (seconds < 0) return QString();
    if (seconds < 60) return QStringLiteral("under a minute left");
    const qint64 m = (seconds + 30) / 60;
    return m == 1 ? QStringLiteral("about a minute left") : QStringLiteral("about %1 minutes left").arg(m);
}

SetupPage::SetupPage(LauncherApp* app, DetailsSheet* details, QWidget* parent) : QWidget(parent), app_(app), detailsSheet_(details) {
    setAcceptDrops(true);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(LauncherTheme::S6, LauncherTheme::S5, LauncherTheme::S6, LauncherTheme::S4);
    col->setSpacing(LauncherTheme::S3);
    header_ = new PageHeader(QStringLiteral("Set up your game"),
                             QStringLiteral("Choose your own disc image, say where the game should live and how it should look, then build it. "
                                            "Nothing from the game comes with this application; it is made from your disc, on this computer, once."),
                             this);
    col->addWidget(header_);
    scroll_ = new QScrollArea(this);
    QScrollArea* scroll = scroll_;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("setup_scroll"));
    auto* inner = new QWidget();
    inner->setAcceptDrops(false);
    auto* body = new QVBoxLayout(inner);
    body->setContentsMargins(0, 0, LauncherTheme::S3, 0);
    body->setSpacing(LauncherTheme::S2);
    body->addWidget(buildYourGame(inner));
    body->addSpacing(LauncherTheme::S3);
    body->addWidget(buildWhereItLives(inner));
    body->addSpacing(LauncherTheme::S3);
    body->addWidget(buildSettings(inner));
    body->addSpacing(LauncherTheme::S2);
    body->addWidget(buildAdvanced(inner));
    body->addStretch(1);
    scroll->setWidget(inner);
    col->addWidget(scroll, 1);
    col->addWidget(buildBottom(this));

    connect(app_, &LauncherApp::foldersChanged, this, [this] { refreshFolder(); refreshButtons(); });
    connect(app_->library(), &GameLibrary::changed, this, [this] { refreshDisc(); refreshButtons(); });
    connect(app_, &LauncherApp::stateChanged, this, [this] { if (card_) card_->refresh(); });
    connect(app_->preparer(), &PrepareRunner::progress, this, [this](double fraction, qint64 eta, const QString& phase) {
        if (state_ != Building) return;
        progress_->setText(phase);
        progress_->setProgress(int(fraction * 1000), 1000);
        progress_->setCaption(QStringLiteral("%1% · %2").arg(int(fraction * 100)).arg(etaText(eta).isEmpty() ? QStringLiteral("time left not known yet") : etaText(eta)));
    });
    connect(app_->preparer(), &PrepareRunner::failed, this, [this](const QString& text, const QString& log) {
        if (state_ != Building) return;
        lastLog_ = log;
        setError(text);
        setState(Failed);
    });
    connect(app_->preparer(), &PrepareRunner::finished, this, [this](const QString&) {
        if (state_ != Building) return;
        // the pipeline writes a fresh options file: the player's choices from this window go in after it
        flushSettings();
        progress_->setProgress(1000, 1000);
        progress_->setText(QStringLiteral("Your game is ready"));
        progress_->setCaption(QStringLiteral("%1 is in %2. Press Play, or start it from that folder whenever you like.")
                                  .arg(app_->manifest()->name(), AppData::shortPath(app_->gameFolder()->path())));
        setState(Done);
    });
    setState(Idle);
}

QLabel* SetupPage::sectionTitle(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setFont(LauncherTheme::instance().font(LauncherTheme::Title));
    l->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::Text)));
    return l;
}

QWidget* SetupPage::buildYourGame(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(sectionTitle(QStringLiteral("Your game"), box));
    col->addWidget(mutedLabel(QStringLiteral("A disc image is a copy of your own disc, usually a .iso file. It is recognised by what is on it, never by its name."), box, LauncherTheme::Caption));
    auto* row = new QHBoxLayout();
    row->setSpacing(LauncherTheme::S2);
    discField_ = new QLineEdit(box);
    discField_->setObjectName(QStringLiteral("setup_disc_path"));
    discField_->setPlaceholderText(QStringLiteral("Drop your disc image here, or choose it…"));
    discField_->setAccessibleName(QStringLiteral("Disc image"));
    discField_->setAcceptDrops(false);   // a drop anywhere on the window is the page's
    discField_->setClearButtonEnabled(false);
    discField_->setMaximumWidth(720);
    connect(discField_, &QLineEdit::returnPressed, this, [this] { if (!discField_->text().trimmed().isEmpty()) chooseDisc(discField_->text().trimmed()); });
    row->addWidget(discField_, 1);
    choose_ = new FlatButton(QStringLiteral("Choose…"), FlatButton::Secondary, box);
    choose_->setObjectName(QStringLiteral("setup_choose_disc"));
    connect(choose_, &QAbstractButton::clicked, this, [this] {
        QStringList filters;
        for (const QString& e : app_->manifest()->discExtensions()) filters << "*" + e;
        const QString start = disc_.path.isEmpty() ? AppData::homePath() : QFileInfo(disc_.path).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose your disc image of %1").arg(app_->manifest()->name()), start,
                                                          QStringLiteral("Disc images (%1)").arg(filters.join(' ')));
        if (!path.isEmpty()) chooseDisc(path);
    });
    row->addWidget(choose_);
    row->addStretch(1);
    col->addLayout(row);
    cardHost_ = new QWidget(box);
    cardHost_->setMaximumWidth(720);
    auto* cardCol = new QVBoxLayout(cardHost_);
    cardCol->setContentsMargins(0, 0, 0, 0);
    col->addWidget(cardHost_);
    discStatus_ = new QLabel(box);
    discStatus_->setObjectName(QStringLiteral("setup_disc_status"));
    discStatus_->setFont(LauncherTheme::instance().font(LauncherTheme::Body));
    discStatus_->setWordWrap(true);
    discStatus_->setMaximumWidth(720);
    col->addWidget(discStatus_);
    return box;
}

QWidget* SetupPage::buildWhereItLives(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    col->addWidget(sectionTitle(QStringLiteral("Where the game lives"), box));
    folder_ = new FolderRow(QStringLiteral("Game folder"), box);
    folder_->setObjectName(QStringLiteral("setup_folder"));
    folder_->setMaximumWidth(720);
    connect(folder_, &FolderRow::changeRequested, this, [this] {
        const QString start = app_->gameFolder()->chosen() ? QFileInfo(app_->gameFolder()->path()).absolutePath() : app_->gameFolder()->defaultParent();
        const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose where the game should live"), start);
        if (!picked.isEmpty()) chooseFolder(picked);
    });
    col->addWidget(folder_);
    const QString note = app_->manifest()->portableNote();
    folderNote_ = mutedLabel(note.isEmpty() ? QStringLiteral("One folder holds the game, its options and its saves.") : note, box, LauncherTheme::Caption);
    folderNote_->setMaximumWidth(720);
    col->addWidget(folderNote_);
    return box;
}

QWidget* SetupPage::buildSettings(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S1);
    col->addWidget(sectionTitle(QStringLiteral("Settings"), box));
    col->addWidget(mutedLabel(QStringLiteral("The same settings the game shows in its own menu. You can change them any time later."), box, LauncherTheme::Caption));
    for (const SettingsGroupSpec& g : app_->manifest()->settingsGroups()) {
        auto* title = new QLabel(g.title, box);
        title->setFont(LauncherTheme::instance().font(LauncherTheme::Label));
        title->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::TextMuted)));
        col->addSpacing(LauncherTheme::S2);
        col->addWidget(title);
        for (const SettingsRowSpec& spec : g.rows) {
            auto* row = new SettingsRow(forThisMachine(spec), box);
            connect(row, &SettingsRow::valueChanged, this, [this](const QString& key, const QString& value) {
                pending_[key] = value;
                // a game folder already chosen (a returning player): the file is written at once, as on the Settings page
                if (app_->gameFolder()->chosen()) { QString err; if (app_->setSettingValue(key, value, &err)) pending_.remove(key); }
                applyLocks();
            });
            rows_ << row;
            col->addWidget(row);
        }
    }
    return box;
}

QWidget* SetupPage::buildAdvanced(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    advancedToggle_ = new FlatButton(QStringLiteral("Advanced"), FlatButton::Quiet, box);
    advancedToggle_->setObjectName(QStringLiteral("setup_advanced"));
    advancedToggle_->setCheckable(true);
    col->addWidget(advancedToggle_, 0, Qt::AlignLeft);
    advancedBox_ = new QWidget(box);
    auto* adv = new QVBoxLayout(advancedBox_);
    adv->setContentsMargins(0, 0, 0, 0);
    adv->setSpacing(LauncherTheme::S2);
    auto* which = new QLabel(QStringLiteral("Which computer"), advancedBox_);
    which->setFont(LauncherTheme::instance().font(LauncherTheme::Label));
    adv->addWidget(which);
    targets_ = new SegmentedControl(advancedBox_);
    targets_->setObjectName(QStringLiteral("setup_targets"));
    QStringList labels;
    for (const TargetSpec& t : app_->manifest()->targets()) { labels << t.label; targetIds_ << t.id; }
    targets_->setItems(labels, targetIds_);
    targets_->setAccessibleName(QStringLiteral("Which computer"));
    connect(targets_, &SegmentedControl::valueChanged, this, [this](const QString&) { refreshTarget(); refreshButtons(); });
    adv->addWidget(targets_, 0, Qt::AlignLeft);
    targetNote_ = mutedLabel(QString(), advancedBox_, LauncherTheme::Caption);
    targetNote_->setMaximumWidth(720);
    adv->addWidget(targetNote_);
    advancedBox_->setVisible(false);
    col->addWidget(advancedBox_);
    connect(advancedToggle_, &QAbstractButton::toggled, this, [this](bool on) {
        advancedBox_->setVisible(on);
        if (on) QTimer::singleShot(0, this, [this] { scroll_->ensureWidgetVisible(targetNote_); });   // the opened section scrolls into view
    });
    return box;
}

QWidget* SetupPage::buildBottom(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    progress_ = new ProgressPanel(box);
    progress_->setObjectName(QStringLiteral("setup_progress"));
    col->addWidget(progress_);
    error_ = new QLabel(box);
    error_->setObjectName(QStringLiteral("setup_error"));
    error_->setFont(LauncherTheme::instance().font(LauncherTheme::Body));
    error_->setWordWrap(true);
    col->addWidget(error_);
    auto* row = new QHBoxLayout();
    row->setSpacing(LauncherTheme::S2);
    buildHint_ = mutedLabel(QString(), box, LauncherTheme::Caption);
    buildHint_->setObjectName(QStringLiteral("setup_build_hint"));
    row->addWidget(buildHint_, 1);
    row->addStretch(1);
    stop_ = new FlatButton(QStringLiteral("Stop"), FlatButton::Secondary, box);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet, box);
    retry_ = new FlatButton(QStringLiteral("Try again"), FlatButton::Primary, box);
    leave_ = new FlatButton(QStringLiteral("Not now"), FlatButton::Quiet, box);
    leave_->setAccessibleName(QStringLiteral("Leave the setup for now"));
    play_ = new FlatButton(QStringLiteral("Play"), FlatButton::Primary, box);
    play_->setLarge(true);
    play_->setObjectName(QStringLiteral("setup_play"));
    build_ = new FlatButton(QStringLiteral("Build the game"), FlatButton::Primary, box);
    build_->setLarge(true);
    build_->setObjectName(QStringLiteral("setup_build"));
    for (FlatButton* b : {stop_, details_, leave_, retry_, play_, build_}) row->addWidget(b);
    col->addLayout(row);
    connect(build_, &QAbstractButton::clicked, this, &SetupPage::build);
    connect(retry_, &QAbstractButton::clicked, this, &SetupPage::build);
    connect(stop_, &QAbstractButton::clicked, this, &SetupPage::stop);
    connect(play_, &QAbstractButton::clicked, this, &SetupPage::play);
    connect(leave_, &QAbstractButton::clicked, this, &SetupPage::leave);
    connect(details_, &QAbstractButton::clicked, this, [this] {
        const QString log = app_->preparer()->logPath();
        if (!log.isEmpty() && QFileInfo::exists(log)) detailsSheet_->showLogFile(QStringLiteral("Details"), log);
        else detailsSheet_->showLog(QStringLiteral("Details"), lastLog_.isEmpty() ? QStringLiteral("Nothing has been written yet.") : lastLog_);
    });
    return box;
}

void SetupPage::open(const QString& isoPath) {
    errorText_.clear();
    error_->clear();
    progress_->clear();
    lastLog_.clear();
    setState(Idle);
    if (!isoPath.isEmpty()) chooseDisc(isoPath);
    else { disc_ = DiscIdentity(); discField_->clear(); refreshDisc(); }
    refreshFolder();
    refreshSettingsRows();
    if (targets_->currentIndex() < 0) targets_->setCurrentValue(app_->hostTarget());
    refreshTarget();
    refreshButtons();
}

void SetupPage::chooseDisc(const QString& path) {
    disc_ = app_->addDisc(path);
    disc_.path = path;
    discField_->setText(QDir::toNativeSeparators(path));
    if (disc_.playable()) app_->setCurrentDisc(path);
    refreshDisc();
    refreshButtons();
}

void SetupPage::refreshDisc() {
    const LauncherTheme& t = LauncherTheme::instance();
    if (card_) { cardHost_->layout()->removeWidget(card_); card_->deleteLater(); card_ = nullptr; }
    if (disc_.path.isEmpty()) {
        discStatus_->setText(QStringLiteral("No disc image chosen yet."));
        discStatus_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
        discStatus_->setVisible(true);
        cardHost_->setVisible(false);
        return;
    }
    if (disc_.recognised() && app_->library()->game(disc_.path)) {
        card_ = new GameCard(app_, disc_.path, cardHost_);
        card_->setObjectName(QStringLiteral("setup_disc_card"));
        card_->setSelectable(true);
        card_->setSelected(disc_.playable());
        cardHost_->layout()->addWidget(card_);
        cardHost_->setVisible(true);
        discStatus_->setVisible(false);
        return;
    }
    // not a disc image / unreadable / not this game: the reason in words, never refused silently
    cardHost_->setVisible(false);
    discStatus_->setText(disc_.reason.isEmpty() ? QStringLiteral("This file could not be read as a disc image.") : disc_.reason);
    discStatus_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Error)));
    discStatus_->setVisible(true);
}

QString SetupPage::folderPath() const {
    return app_->gameFolder()->chosen() ? app_->gameFolder()->path() : app_->gameFolder()->suggestedPath();
}

void SetupPage::chooseFolder(const QString& folder) {
    app_->chooseGameFolder(folder);
    flushSettings();
}

void SetupPage::refreshFolder() { folder_->setPath(folderPath()); }

void SetupPage::refreshSettingsRows() {
    for (SettingsRow* r : rows_) {
        const QString key = r->spec().key;
        r->setValue(pending_.contains(key) ? pending_.value(key) : app_->settingValue(key));
    }
    applyLocks();
}

SettingsRow* SetupPage::settingsRow(const QString& key) const {
    for (SettingsRow* r : rows_) if (r->spec().key == key) return r;
    return nullptr;
}

void SetupPage::applyLocks() {
    for (SettingsRow* r : rows_) {
        const SettingsLock& lock = r->spec().lockedBy;
        if (lock.key.isEmpty()) continue;
        SettingsRow* other = settingsRow(lock.key);
        const bool locked = other ? other->value() == lock.value : app_->settingValue(lock.key) == lock.value;
        r->setLocked(locked);
        r->setHelpOverride(locked ? lock.help : QString());
    }
}

void SetupPage::flushSettings() {
    if (!app_->gameFolder()->chosen()) return;
    for (auto it = pending_.begin(); it != pending_.end();) {
        QString err;
        if (app_->setSettingValue(it.key(), it.value(), &err)) it = pending_.erase(it);
        else ++it;
    }
}

QString SetupPage::selectedTarget() const {
    const QString v = targets_->currentValue();
    return v.isEmpty() ? app_->hostTarget() : v;
}

void SetupPage::setAdvancedOpen(bool open) { advancedToggle_->setChecked(open); }
bool SetupPage::advancedOpen() const { return advancedToggle_->isChecked(); }

void SetupPage::refreshTarget() {
    const QString host = app_->hostTarget();
    const QString id = selectedTarget();
    const TargetSpec* t = app_->manifest()->target(id);
    const QString label = t ? t->label : id;
    QString why;
    const bool can = app_->preparer()->available(id, &why);
    QString note = id == host ? QStringLiteral("This computer (%1). The game is built here and plays here.").arg(label)
                              : QStringLiteral("Another computer (%1).").arg(label);
    if (!can && !why.isEmpty()) note += QStringLiteral(" %1").arg(why);
    targetNote_->setText(note);
    targetNote_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(can ? LauncherTheme::TextMuted : LauncherTheme::Warn)));
    if (id != host && !advancedToggle_->isChecked()) advancedToggle_->setChecked(true);
}

bool SetupPage::buildEnabled(QString* why) const {
    if (state_ == Building) { if (why) *why = QStringLiteral("The game is being built."); return false; }
    if (!app_->canPrepare(disc_.path, why)) return false;
    QString w;
    if (!app_->preparer()->available(selectedTarget(), &w)) { if (why) *why = w; return false; }
    return true;
}

void SetupPage::setState(State state) {
    state_ = state;
    refreshButtons();
}

void SetupPage::refreshButtons() {
    const bool idle = state_ == Idle, building = state_ == Building, failed = state_ == Failed, done = state_ == Done;
    QString why;
    const bool can = buildEnabled(&why);
    build_->setVisible(idle);
    build_->setEnabled(can);
    buildHint_->setText(idle && !can ? why : QString());
    buildHint_->setVisible(idle && !can);
    stop_->setVisible(building);
    details_->setVisible(building || failed);
    retry_->setVisible(failed);
    play_->setVisible(done && selectedTarget() == app_->hostTarget());
    leave_->setVisible(idle || failed || done);
    leave_->setText(done ? QStringLiteral("Go to the launcher") : QStringLiteral("Not now"));
    progress_->setVisible(building || done);
    error_->setVisible(failed);
    // the inputs are frozen while building
    for (QWidget* w : QList<QWidget*>{discField_, choose_, folder_, targets_}) w->setEnabled(!building);
    for (SettingsRow* r : rows_) r->setEnabled(!building);
}

void SetupPage::setError(const QString& text) {
    errorText_ = text;
    error_->setText(text);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(LauncherTheme::instance().css(LauncherTheme::Error)));
}

void SetupPage::build() {
    QString why;
    if (state_ != Idle && state_ != Failed) return;
    if (!app_->canPrepare(disc_.path, &why)) { setError(why); setState(Failed); return; }
    if (!app_->gameFolder()->chosen()) app_->chooseGameFolder(app_->gameFolder()->suggestedPath(), true);
    QString err;
    if (!app_->gameFolder()->makeSubfolders(&err)) { setError(err); setState(Failed); return; }
    flushSettings();   // the player's choices are in the options file even if building fails
    errorText_.clear();
    error_->clear();
    const QString estimate = app_->preparer()->estimateText(selectedTarget());
    progress_->setText(QStringLiteral("Preparing your game"));
    progress_->setIndeterminate(true);
    progress_->setCaption(estimate.isEmpty() ? QStringLiteral("time left not known yet") : QStringLiteral("usually %1").arg(estimate));
    setState(Building);
    if (!app_->prepare(disc_.path, selectedTarget(), &err)) { setError(err); setState(Failed); }
}

void SetupPage::stop() {
    app_->preparer()->cancel();
}

void SetupPage::play() {
    QString err;
    if (!app_->play(app_->currentDisc(), &err)) { setError(err); setState(Failed); return; }
    emit completed();
}

void SetupPage::leave() {
    flushSettings();
    emit completed();
}

void SetupPage::dragEnterEvent(QDragEnterEvent* event) {
    if (state_ != Building && event->mimeData()->hasUrls()) { event->acceptProposedAction(); dragOver_ = true; update(); }
}

void SetupPage::dragLeaveEvent(QDragLeaveEvent*) { dragOver_ = false; update(); }

void SetupPage::dropEvent(QDropEvent* event) {
    dragOver_ = false;
    update();
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile()) { chooseDisc(u.toLocalFile()); break; }
    }
    event->acceptProposedAction();
}

void SetupPage::paintEvent(QPaintEvent*) {
    if (!dragOver_) return;
    const LauncherTheme& t = LauncherTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(t.color(LauncherTheme::Accent), 2, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(6, 6, -6, -6), LauncherTheme::radiusCard(), LauncherTheme::radiusCard());
    p.setFont(t.font(LauncherTheme::Title));
    p.setPen(t.color(LauncherTheme::Accent));
    p.drawText(rect().adjusted(0, 0, 0, -LauncherTheme::S6), Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral("Drop the disc image to choose it"));
}

}  // namespace ps2r::launcher
