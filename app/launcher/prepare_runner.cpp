// app/launcher/prepare_runner.cpp -- createDefault(): the fake for the screens, the bundled pipeline when the launcher
// was built with it (PS2R_LAUNCHER_HAS_PREPARE), else the honest "cannot prepare" runner.
#include "prepare_runner.h"
#include "fake_prepare_runner.h"
#include "unavailable_prepare_runner.h"
#ifdef PS2R_LAUNCHER_HAS_PREPARE
#include "bundled_prepare_runner.h"
#endif
namespace ps2r::launcher {

PrepareRunner* PrepareRunner::createDefault(const TitleManifest* manifest, QObject* parent) {
    const QByteArray fake = qgetenv("PS2R_LAUNCHER_FAKE_PREPARE");
    if (!fake.isEmpty()) return new FakePrepareRunner(QString::fromUtf8(fake), parent);
#ifdef PS2R_LAUNCHER_HAS_PREPARE
    return new BundledPrepareRunner(manifest, parent);
#else
    Q_UNUSED(manifest);
    return new UnavailablePrepareRunner(parent);
#endif
}

}  // namespace ps2r::launcher
