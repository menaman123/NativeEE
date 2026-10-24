// app/launcher/unavailable_prepare_runner.cpp -- the honest "cannot prepare here" runner.
#include "unavailable_prepare_runner.h"
#include <QFile>
#include <QTimer>
namespace ps2r::launcher {

namespace {
const char* kWhy = "This copy of the launcher cannot prepare the game: the part that does it is not beside the application. "
                   "Download the complete release for your computer and open the launcher from it.";
}

UnavailablePrepareRunner::UnavailablePrepareRunner(QObject* parent) : PrepareRunner(parent) {}

bool UnavailablePrepareRunner::available(const QString&, QString* why) const {
    if (why) *why = QString::fromLatin1(kWhy);
    return false;
}

QString UnavailablePrepareRunner::estimateText(const QString&) const { return QString(); }

bool UnavailablePrepareRunner::start(const PrepareRequest& request, QString* error) {
    logPath_ = request.logPath;
    QFile f(logPath_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray("prepare: no pipeline is bundled with this launcher\n"));
        f.write(QByteArray("disc: ") + request.isoPath.toUtf8() + "\nfolder: " + request.gameFolder.toUtf8() + "\n");
    }
    if (error) *error = QString::fromLatin1(kWhy);
    // reported asynchronously so the page is in its running state when the failure arrives, as with a real runner
    QTimer::singleShot(0, this, [this] { emit failed(QString::fromLatin1(kWhy), QStringLiteral("no pipeline is bundled with this launcher")); });
    return true;
}

}  // namespace ps2r::launcher
