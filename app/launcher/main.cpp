// app/launcher/main.cpp -- the launcher's entry: the style, the manifest, the window; `--manifest PATH` names the
// title manifest (else titles/*.json beside the application), `--no-scan` skips the disc scan at start, `--identify IMAGE`
// prints what the image is by its content (status, region, id, checksum, reason) and exits 0 when it is the supported
// disc -- the check a packaging step or a judge runs without a window.
#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QTextStream>
#include <QVBoxLayout>
#include "disc_identifier.h"
#include "launcher_app.h"
#include "launcher_style.h"
#include "launcher_theme.h"
#include "main_window.h"
#include "title_manifest.h"

int main(int argc, char** argv) {
    QApplication::setApplicationName("NativeEE Launcher");
    QApplication::setOrganizationName("NativeEE");
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("The launcher: pick the game, press Play.");
    parser.addHelpOption();
    const QCommandLineOption manifestOpt("manifest", "The game description file.", "path");
    const QCommandLineOption noScanOpt("no-scan", "Do not look for disc images at start.");
    const QCommandLineOption identifyOpt("identify", "Say what a disc image is, by its content, and exit.", "image");
    parser.addOption(manifestOpt);
    parser.addOption(noScanOpt);
    parser.addOption(identifyOpt);
    parser.process(app);

    ps2r::launcher::LauncherTheme::instance();
    ps2r::launcher::LauncherStyle::apply();
    ps2r::launcher::LauncherApp launcher;
    QString error;
    if (!launcher.loadManifest(parser.value(manifestOpt), &error)) {
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
        return app.exec();
    }
    if (parser.isSet(identifyOpt)) {
        const ps2r::launcher::DiscIdentity d = ps2r::launcher::DiscIdentifier::identify(*launcher.manifest(), parser.value(identifyOpt));
        QTextStream out(stdout);
        out << "status=" << ps2r::launcher::DiscIdentity::statusKey(d.status) << "\n"
            << "file=" << d.fingerprintFile << "\nregion=" << d.region << "\nid=" << d.id << "\nsha256=" << d.sha256   // lint: allow
            << "\nvolume=" << d.volumeId << "\nreason=" << d.reason << "\n";
        return d.playable() ? 0 : 1;
    }
    ps2r::launcher::MainWindow window(&launcher);
    window.show();
    window.startFirstRunIfNeeded();
    if (!parser.isSet(noScanOpt)) launcher.scan();
    return app.exec();
}
