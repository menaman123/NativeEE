// app/launcher/first_run_sheet.cpp -- the first-run sheet.
#include "first_run_sheet.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include "checklist_row.h"
#include "details_sheet.h"
#include "flat_button.h"
#include "installer.h"
#include "launcher_theme.h"
#include "progress_panel.h"
#include "sheet_overlay.h"
namespace ps2r::launcher {

FirstRunSheet::FirstRunSheet(Installer* installer, DetailsSheet* details, QWidget* host)
    : QWidget(host), installer_(installer), detailsSheet_(details) {
    hide();
    const LauncherTheme& t = LauncherTheme::instance();
    auto* content = new QWidget();
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(LauncherTheme::S2);
    auto* intro = new QLabel(installer_->buildFromSource()
                                 ? QStringLiteral("Building from source: the launcher installs the compiler and tools it needs into its app-data folder, then builds the game here.")
                                 : QStringLiteral("The launcher downloads the finished game into the folder you chose. Nothing else is installed on your computer."), content);
    intro->setFont(t.font(LauncherTheme::Body));
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    col->addWidget(intro);
    col->addSpacing(LauncherTheme::S2);
    const QVector<InstallStep>& steps = installer_->steps();
    for (int i = 0; i < steps.size(); ++i) {
        auto* row = new ChecklistRow(steps[i], content);
        row->setObjectName(QStringLiteral("checklist_%1").arg(steps[i].id));
        connect(row, &ChecklistRow::retryRequested, this, [this, i] { installer_->retry(i); });
        connect(row, &ChecklistRow::detailsRequested, this, [this] { detailsSheet_->showLogFile(QStringLiteral("Details"), installer_->currentLogPath()); });
        connect(row, &ChecklistRow::installRequested, this, [this, i] { installer_->promptInstall(i); });
        rows_ << row;
        col->addWidget(row);
    }
    col->addSpacing(LauncherTheme::S3);
    progress_ = new ProgressPanel(content);
    col->addWidget(progress_);
    col->addSpacing(LauncherTheme::S2);
    totals_ = new QLabel(content);
    totals_->setFont(t.font(LauncherTheme::Caption));
    totals_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
    col->addWidget(totals_);
    overlay_ = new SheetOverlay(QStringLiteral("First run"), content, host);
    overlay_->setPanelWidth(720);
    details_ = new FlatButton(QStringLiteral("Details"), FlatButton::Quiet);
    close_ = new FlatButton(QStringLiteral("Close"), FlatButton::Secondary);
    close_->setAccessibleName(QStringLiteral("Close the first-run sheet"));
    overlay_->addButton(details_);
    overlay_->addButton(close_);
    overlay_->setCancelButton(close_);
    connect(details_, &QAbstractButton::clicked, this, [this] { detailsSheet_->showLogFile(QStringLiteral("Details"), installer_->currentLogPath()); });
    connect(close_, &QAbstractButton::clicked, this, [this] { overlay_->close(); });
    connect(installer_, &Installer::stepChanged, this, [this](int i) {
        if (i >= 0 && i < rows_.size()) rows_[i]->setStep(installer_->steps()[i]);
        refreshTotals();
    });
    connect(installer_, &Installer::progress, this, [this](const QString& text, int done, int total, bool indeterminate) {
        progress_->setText(text);
        if (text.isEmpty()) progress_->clear();
        else if (indeterminate) progress_->setIndeterminate(true);
        else progress_->setProgress(done, total);
    });
    connect(installer_, &Installer::logLine, this, [this](const QString& l) { detailsSheet_->appendLine(l); });
    connect(installer_, &Installer::finished, this, [this](bool ok) {
        refreshTotals();
        if (ok && installer_->complete()) QTimer::singleShot(600, this, [this] { if (overlay_->isVisible()) overlay_->close(); });
    });
    auto* ticker = new QTimer(this);
    ticker->setInterval(1000);
    connect(ticker, &QTimer::timeout, this, [this] {
        if (!installer_->running()) return;
        for (int i = 0; i < installer_->steps().size(); ++i) {
            if (installer_->steps()[i].state == InstallStep::Running) {
                progress_->setCaption(QStringLiteral("%1 · %2").arg(installer_->steps()[i].name, progress_->text().isEmpty() ? QString() : QStringLiteral("running")));
            }
        }
    });
    ticker->start();
    refreshTotals();
}

void FirstRunSheet::open() {
    for (int i = 0; i < rows_.size(); ++i) rows_[i]->setStep(installer_->steps()[i]);
    refreshTotals();
    overlay_->open();
}

bool FirstRunSheet::isOpen() const { return overlay_->isVisible(); }
void FirstRunSheet::close() { overlay_->close(); }

void FirstRunSheet::refreshTotals() {
    qint64 onDisk = 0;
    for (const InstallStep& s : installer_->steps()) onDisk += s.bytes;
    totals_->setText(QStringLiteral("Total %1 downloaded · %2 on disk").arg(Installer::bytesText(installer_->totalDownloadedBytes()), Installer::bytesText(onDisk)));
}

}  // namespace ps2r::launcher
