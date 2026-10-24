// app/launcher/main.cpp -- the launcher's entry: the style, the manifest, the window. `--manifest PATH` names the title
// manifest (else titles/*.json beside the application); `--identify IMAGE` prints what the image is by its content
// (status, region, id, checksum, reason) and exits 0 when it is the supported disc -- the check a packaging step or a
// judge runs without a window; `--index URL` names the release index the Build step reads (the manifest's by default;
// an offline copy for a judge); `--prepare --index URL --iso IMAGE --folder DIR [--jobs N] [--release TAG]
// [--json REPORT]` is the headless Build: no window, progress on stderr, the game's path on stdout, the same report the
// command-line tool writes, exit 0 when the game is ready. The launcher never looks for disc images by itself (D-L10).
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QTextStream>
#include <QVBoxLayout>
#include <memory>
#include "disc_identifier.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "main_window.h"
#include "prepare_runner.h"
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
                const QString& release, int jobs, const QString& reportPath) {
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
    const QCommandLineOption isoOpt("iso", "The disc image (--prepare).", "image");
    const QCommandLineOption folderOpt("folder", "The game folder (--prepare).", "dir");
    const QCommandLineOption jobsOpt("jobs", "How many things to do at once (--prepare).", "n");
    const QCommandLineOption jsonOpt("json", "Write the report here (--prepare).", "report");   // lint: allow
    parser.addOptions({manifestOpt, identifyOpt, indexOpt, releaseOpt, prepareOpt, isoOpt, folderOpt, jobsOpt, jsonOpt});
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
                           parser.value(jobsOpt).toInt(), parser.value(jsonOpt));
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
    window.startSetupIfNeeded();
    return app->exec();
}
