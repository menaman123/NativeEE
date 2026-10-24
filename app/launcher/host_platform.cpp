// app/launcher/host_platform.cpp -- this machine's os and architecture.
#include "host_platform.h"
#include <QSysInfo>
#include "title_manifest.h"
namespace ps2r::launcher {

QString HostPlatform::os() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString HostPlatform::arch() {
    const QString a = QSysInfo::currentCpuArchitecture();
    if (a == QLatin1String("arm64") || a == QLatin1String("aarch64")) return QStringLiteral("arm64");
    if (a == QLatin1String("x86_64") || a == QLatin1String("amd64") || a == QLatin1String("x64")) return QStringLiteral("x86_64");
    return a;
}

QString HostPlatform::osLabel() {
    const QString o = os();
    if (o == QLatin1String("macos")) return QStringLiteral("macOS");
    if (o == QLatin1String("windows")) return QStringLiteral("Windows");
    return QStringLiteral("Linux");
}

QString HostPlatform::targetId(const TitleManifest& manifest) {
    for (const TargetSpec& t : manifest.targets()) if (t.os == os() && t.arch == arch()) return t.id;
    for (const TargetSpec& t : manifest.targets()) if (t.os == os()) return t.id;   // a near match (the os at least)
    return QString();
}

}  // namespace ps2r::launcher
