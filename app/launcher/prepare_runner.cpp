// app/launcher/prepare_runner.cpp -- createDefault() and the runner a launcher has when no real one is bundled.
#include "prepare_runner.h"
#include <QTimer>
#include "fake_prepare_runner.h"
#include "unavailable_prepare_runner.h"
namespace ps2r::launcher {

PrepareRunner* PrepareRunner::createDefault(const TitleManifest* manifest, QObject* parent) {
    Q_UNUSED(manifest);
    const QByteArray fake = qgetenv("PS2R_LAUNCHER_FAKE_PREPARE");
    if (!fake.isEmpty()) return new FakePrepareRunner(QString::fromUtf8(fake), parent);
    // The prepare track replaces this line with its runner (the bundled pipeline beside the launcher); until then the
    // launcher is honest about it on the Preparing page.
    return new UnavailablePrepareRunner(parent);
}

}  // namespace ps2r::launcher
