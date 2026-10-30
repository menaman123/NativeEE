// app/launcher/main.cpp -- the launcher's entry: the style, the manifest, the window. `--manifest PATH` names the title
// manifest (else titles/*.json beside the application); `--identify IMAGE` prints what the image is by its content
// (status, region, id, checksum, reason) and exits 0 when it is the supported disc -- the check a packaging step or a
// judge runs without a window; `--index URL` names the release index the Build step reads (the manifest's by default;
// an offline copy for a judge); `--prepare --index URL --iso IMAGE --folder DIR [--jobs N] [--release TAG]
// [--json REPORT] [--subfolder memcards=DIR] [--subfolder saves=DIR]` is the headless Build: no window, progress on
// stderr, the game's path on stdout, the same report the command-line tool writes, exit 0 when the game is ready; `--iso IMAGE` alone opens the setup window on that disc. The
// launcher never looks for disc images by itself (D-L10). With a window it first looks for a newer release of itself
// (SelfUpdater; `--no-update` skips that) and refreshes the game folder after an update (LauncherApp::refreshGame).
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <memory>
#include "app_data.h"
#include "disc_identifier.h"
#include "game_folder.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "main_window.h"
#include "prepare_runner.h"
#include "self_updater.h"
#include "title_manifest.h"
#ifdef PS2R_LAUNCHER_HAS_PREPARE
#include "bundled_prepare_runner.h"
#endif

namespace {
QString etaLine(qint64 seconds) {
    if (seconds < 0) return QString();
    if (seconds < 60) return QStringLiteral("less than a minute left");
    const qint64 m = (seconds + 30) / 60;
    return QStringLiteral("about %1 minute%2 left").arg(m).arg(m == 1 ? "" : "s");
}

// The headless Build: the judge's driver (`--prepare`). Returns the process exit code.
int runHeadless(ps2r::launcher::LauncherApp& launcher, const QString& iso, const QString& folder, const QString& index,
                const QString& release, int jobs, const QString& reportPath, const QStringList& subfolders) {
    QTextStream err(stderr), out(stdout);
    auto writeReport = [&](const QString& text) {
        if (reportPath.isEmpty()) return;
        QFile f(reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(text.toUtf8());
    };
    if (iso.isEmpty() || folder.isEmpty()) { err << "launcher --prepare: --iso and --folder are required\n"; return 2; }
#ifdef PS2R_LAUNCHER_HAS_PREPARE
    auto* bundled = dynamic_cast<ps2r::launcher::BundledPrepareRunner*>(launcher.preparer());
#else
    ps2r::launcher::PrepareRunner* bundled = nullptr;
#endif
    if (!bundled) {
        err << "launcher --prepare: this launcher carries no prepare pipeline\n";
        writeReport(QStringLiteral("{ \"ok\": false, \"failure\": \"no prepare pipeline in this launcher\" }\n"));   // lint: allow
        return 1;
    }
#ifdef PS2R_LAUNCHER_HAS_PREPARE
    if (!index.isEmpty()) bundled->setIndexUrl(index);
    if (!release.isEmpty()) bundled->setReleaseTag(release);
    if (jobs > 0) bundled->setJobs(jobs);
    const ps2r::launcher::DiscIdentity d = launcher.addDisc(QFileInfo(iso).absoluteFilePath());
    if (!d.playable()) {
        err << d.reason << "\n";
        writeReport(QStringLiteral("{ \"ok\": false, \"failed_phase\": \"disc\", \"failure\": \"%1\" }\n").arg(d.reason));   // lint: allow
        return 1;
    }
    launcher.chooseGameFolder(QFileInfo(folder).absoluteFilePath(), true);
    for (const QString& s : subfolders) {   // --subfolder memcards=DIR: the setup window's folder rows, scripted
        const int eq = s.indexOf('=');
        if (eq <= 0) { err << "launcher --prepare: --subfolder wants id=DIR, got " << s << "\n"; return 2; }
        const QString id = s.left(eq);
        if (launcher.gameFolder()->subfolderKey(id).isEmpty()) { err << "launcher --prepare: no changeable sub-folder named " << id << "\n"; return 2; }
        launcher.setSubfolder(id, QFileInfo(s.mid(eq + 1)).absoluteFilePath());
    }
    int lastPct = -1;
    QString lastLine;
    QObject::connect(bundled, &ps2r::launcher::PrepareRunner::progress, [&](double fraction, qint64 eta, const QString& phase) {
        const int pct = int(fraction * 100 + 0.5);
        const QString line = phase + "  " + etaLine(eta);
        if (pct == lastPct && line == lastLine) return;
        lastPct = pct;
        lastLine = line;
        err << QStringLiteral("[%1%] %2\n").arg(pct, 3).arg(line);
        err.flush();
    });
    QObject::connect(bundled, &ps2r::launcher::PrepareRunner::failed, [&](const QString& text, const QString& log) {
        err << "\n" << text << "\n(details: " << log << ")\n";
        err.flush();
        QCoreApplication::exit(1);
    });
    QObject::connect(bundled, &ps2r::launcher::PrepareRunner::finished, [&](const QString& binary) {
        out << binary << "\n";
        out.flush();
        QCoreApplication::exit(0);
    });
    QString startErr;
    if (!launcher.prepare(d.path, QString(), &startErr)) {
        err << startErr << "\n";
        writeReport(QStringLiteral("{ \"ok\": false, \"failure\": \"%1\" }\n").arg(startErr));   // lint: allow
        return 1;
    }
    const int rc = QCoreApplication::exec();
    bundled->waitFinished();
    writeReport(bundled->reportJson());
    return rc;
#else
    Q_UNUSED(index); Q_UNUSED(release); Q_UNUSED(jobs);
    return 1;
#endif
}

// The start-up update (SelfUpdater): a newer release is downloaded and installed behind a progress window, then the
// launcher closes and the copy script opens the new one. Otherwise -- no newer release, no network, the player's
// "Not now", a failure -- `carryOn` runs: the usual start.
void checkForUpdate(ps2r::launcher::LauncherApp& launcher, QWidget* window, bool allowed, const std::function<void()>& carryOn) {
    using ps2r::launcher::SelfUpdater;
    auto* updater = new SelfUpdater(launcher.manifest(), launcher.version(), launcher.hostTarget(), launcher.appData()->root() + "/update",
                                    launcher.appData()->logPath("update"), window);
    QString why;
    if (!allowed || !updater->enabled(&why)) {
        delete updater;
        carryOn();
        return;
    }
    QObject::connect(updater, &SelfUpdater::noUpdate, window, [updater, carryOn] { updater->deleteLater(); carryOn(); });
    QObject::connect(updater, &SelfUpdater::updateAvailable, window, [updater, window, carryOn](const QString& tag) {
        auto* dialog = new QProgressDialog(window);
        dialog->setWindowTitle(QStringLiteral("Updating"));
        dialog->setLabelText(QStringLiteral("A new version (%1) is out. Getting it…").arg(tag));
        dialog->setCancelButtonText(QStringLiteral("Not now"));
        dialog->setRange(0, 0);
        dialog->setMinimumDuration(0);
        dialog->setAutoClose(false);
        dialog->setAutoReset(false);
        dialog->setMinimumWidth(440);
        dialog->setWindowModality(Qt::WindowModal);
        auto over = std::make_shared<bool>(false);
        QObject::connect(updater, &SelfUpdater::progress, dialog, [dialog, tag](qint64 done, qint64 total, const QString& phase) {
            if (total > 0 && done >= 0) {
                dialog->setRange(0, 1000);
                dialog->setValue(int(done * 1000 / total));
                dialog->setLabelText(QStringLiteral("%1 (%2)\n%3 of %4 MB").arg(phase, tag).arg(done >> 20).arg(total >> 20));
            } else {
                dialog->setRange(0, 0);
                dialog->setLabelText(QStringLiteral("%1 (%2)").arg(phase, tag));
            }
        });
        QObject::connect(dialog, &QProgressDialog::canceled, window, [updater, dialog, carryOn, over] {
            if (*over) return;
            *over = true;
            updater->cancel();
            dialog->deleteLater();
            updater->deleteLater();
            carryOn();
        });
        QObject::connect(updater, &SelfUpdater::failed, window, [updater, dialog, window, carryOn, over](const QString& text) {
            if (*over) return;
            *over = true;
            dialog->deleteLater();
            updater->deleteLater();
            QMessageBox::information(window, QStringLiteral("Update"), text);
            carryOn();
        });
        QObject::connect(updater, &SelfUpdater::restartRequired, window, [over] {
            *over = true;
            QTimer::singleShot(400, qApp, &QCoreApplication::quit);
        });
        dialog->show();
        updater->install();
    });
    updater->check();
}
}  // namespace

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) if (QByteArray(argv[i]) == "--prepare") headless = true;
    QCoreApplication::setApplicationName("NativeEE Launcher");
    QCoreApplication::setOrganizationName("NativeEE");
    std::unique_ptr<QCoreApplication> app(headless ? new QCoreApplication(argc, argv) : new QApplication(argc, argv));
    QCommandLineParser parser;
    parser.setApplicationDescription("The launcher: choose your disc, build the game, press Play.");
    parser.addHelpOption();
    const QCommandLineOption manifestOpt("manifest", "The game description file.", "path");
    const QCommandLineOption identifyOpt("identify", "Say what a disc image is, by its content, and exit.", "image");
    const QCommandLineOption indexOpt("index", "Where the release index is (the game description's by default).", "url");
    const QCommandLineOption releaseOpt("release", "The release to use (the newest by default).", "tag");
    const QCommandLineOption prepareOpt("prepare", "Build the game without a window (with --iso and --folder).");
    const QCommandLineOption isoOpt("iso", "The disc image: opened in the setup window, or built with --prepare.", "image");
    const QCommandLineOption folderOpt("folder", "The game folder (--prepare).", "dir");
    const QCommandLineOption jobsOpt("jobs", "How many things to do at once (--prepare).", "n");
    const QCommandLineOption jsonOpt("json", "Write the report here (--prepare).", "report");   // lint: allow
    const QCommandLineOption subfolderOpt("subfolder", "Where the memory cards or save states go (--prepare), e.g. memcards=DIR; else inside the game folder.", "id=dir");
    const QCommandLineOption noUpdateOpt("no-update", "Do not look for a newer version of the launcher at start.");
    parser.addOptions({manifestOpt, identifyOpt, indexOpt, releaseOpt, prepareOpt, isoOpt, folderOpt, jobsOpt, jsonOpt, subfolderOpt, noUpdateOpt});
    parser.process(*app);

    ps2r::launcher::LauncherApp launcher;
    QString error;
    if (!headless) {
        ps2r::launcher::LauncherTheme::instance();
        ps2r::launcher::LauncherStyle::apply();
    }
    if (!launcher.loadManifest(parser.value(manifestOpt), &error)) {
        if (headless) { QTextStream(stderr) << error << "\n"; return 2; }
        // the one screen without a manifest: say what is missing
        QWidget w;
        w.setWindowTitle("NativeEE Launcher");
        w.setMinimumSize(640, 240);
        auto* col = new QVBoxLayout(&w);
        col->setContentsMargins(32, 32, 32, 32);
        auto* title = new QLabel("This launcher has no game to launch", &w);
        title->setFont(ps2r::launcher::LauncherTheme::instance().font(ps2r::launcher::LauncherTheme::Title));
        auto* body = new QLabel(error + "\n\nA game ships the launcher with its description beside it. Download the complete release for your computer and open the launcher from it.", &w);
        body->setWordWrap(true);
        col->addWidget(title);
        col->addWidget(body);
        col->addStretch(1);
        w.show();
        return app->exec();
    }
    if (parser.isSet(identifyOpt)) {
        const ps2r::launcher::DiscIdentity d = ps2r::launcher::DiscIdentifier::identify(*launcher.manifest(), parser.value(identifyOpt));
        QTextStream out(stdout);
        out << "status=" << ps2r::launcher::DiscIdentity::statusKey(d.status) << "\n"
            << "file=" << d.fingerprintFile << "\nregion=" << d.region << "\nid=" << d.id << "\nsha256=" << d.sha256   // lint: allow
            << "\nvolume=" << d.volumeId << "\nreason=" << d.reason << "\n";
        return d.playable() ? 0 : 1;
    }
    if (headless) {
        return runHeadless(launcher, parser.value(isoOpt), parser.value(folderOpt), parser.value(indexOpt), parser.value(releaseOpt),
                           parser.value(jobsOpt).toInt(), parser.value(jsonOpt), parser.values(subfolderOpt));
    }
#ifdef PS2R_LAUNCHER_HAS_PREPARE
    if (auto* bundled = dynamic_cast<ps2r::launcher::BundledPrepareRunner*>(launcher.preparer())) {
        if (parser.isSet(indexOpt)) bundled->setIndexUrl(parser.value(indexOpt));
        if (parser.isSet(releaseOpt)) bundled->setReleaseTag(parser.value(releaseOpt));
        if (parser.isSet(jobsOpt)) bundled->setJobs(parser.value(jobsOpt).toInt());
    }
#endif
    ps2r::launcher::MainWindow window(&launcher);
    window.show();
    const bool discNamed = parser.isSet(isoOpt);
    if (discNamed) window.showSetup(QFileInfo(parser.value(isoOpt)).absoluteFilePath());   // a disc named on the command line
    checkForUpdate(launcher, &window, !parser.isSet(noUpdateOpt), [&launcher, &window, discNamed] {
        if (discNamed) return;
        // a launcher newer than the game in the game folder copies its game in; else the first run's setup if due
        if (!launcher.refreshGame()) window.startSetupIfNeeded();
    });
    return app->exec();
}
