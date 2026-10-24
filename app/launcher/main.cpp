// app/launcher/main.cpp -- the launcher's entry: the style, the manifest, the window; `--manifest PATH` names the
// title manifest (else titles/*.json beside the application), `--scan` runs the disc scan at start (the default),
// `--no-scan` does not.
#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QVBoxLayout>
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
    const QCommandLineOption manifestOpt("manifest", "The title manifest (title.json).", "path");
    const QCommandLineOption noScanOpt("no-scan", "Do not scan for disc images at start.");
    parser.addOption(manifestOpt);
    parser.addOption(noScanOpt);
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
        auto* body = new QLabel(error + "\n\nA title ships the launcher with its manifest beside it (titles/title.json); start it with --manifest PATH to point at one.", &w);
        body->setWordWrap(true);
        col->addWidget(title);
        col->addWidget(body);
        col->addStretch(1);
        w.show();
        return app.exec();
    }
    ps2r::launcher::MainWindow window(&launcher);
    window.show();
    if (!window.startFirstRunIfNeeded()) {
        if (!parser.isSet(noScanOpt)) launcher.scan();
    } else if (!parser.isSet(noScanOpt)) {
        launcher.scan();
    }
    return app.exec();
}
