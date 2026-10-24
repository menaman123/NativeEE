// app/launcher/download_pins.cpp -- the pin table. Every sha256 was computed from the archive as published.
#include "download_pins.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSysInfo>
namespace ps2r::launcher {

namespace {
const QVector<DownloadPin> kPins = {
    {"cmake", "macos-arm64", "3.31.6",
     "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz",
     "cmake-3.31.6-macos-universal.tar.gz", "330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d",
     "cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake", 78374480},
    {"cmake", "macos-x86_64", "3.31.6",
     "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz",
     "cmake-3.31.6-macos-universal.tar.gz", "330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d",
     "cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake", 78374480},
    {"cmake", "windows-x86_64", "3.31.6",
     "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip",
     "cmake-3.31.6-windows-x86_64.zip", "d163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836",
     "cmake-3.31.6-windows-x86_64/bin/cmake.exe", 46473549},
    {"ninja", "macos-arm64", "1.12.1",
     "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-mac.zip",
     "ninja-mac-1.12.1.zip", "89a287444b5b3e98f88a945afa50ce937b8ffd1dcc59c555ad9b1baf855298c9", "ninja", 281130},
    {"ninja", "macos-x86_64", "1.12.1",
     "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-mac.zip",
     "ninja-mac-1.12.1.zip", "89a287444b5b3e98f88a945afa50ce937b8ffd1dcc59c555ad9b1baf855298c9", "ninja", 281130},
    {"ninja", "windows-x86_64", "1.12.1",
     "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip",
     "ninja-win-1.12.1.zip", "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a", "ninja.exe", 275425},
    // MinGit: the prebuilt, portable Git for Windows (no installer, no administrator approval); only when the machine has no git
    {"git", "windows-x86_64", "2.56.0.2",
     "https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.2/MinGit-2.56.0.2-64-bit.zip",
     "MinGit-2.56.0.2-64-bit.zip", "da35e72aa21c005a5a0d298cfbae110bc1609a815730ea0dde84b01a1b3cd3be", "cmd/git.exe", 39806486},
};
}  // namespace

QString DownloadPins::currentHost() {
    const QString arch = QSysInfo::currentCpuArchitecture();
#ifdef Q_OS_MACOS
    return arch == "arm64" ? "macos-arm64" : "macos-x86_64";
#elif defined(Q_OS_WIN)
    return "windows-x86_64";
#else
    return arch == "arm64" ? "linux-aarch64" : "linux-x86_64";
#endif
}

const QVector<DownloadPin>& DownloadPins::all() { return kPins; }

const DownloadPin* DownloadPins::find(const QString& tool, const QString& host) {
    for (const DownloadPin& p : kPins) if (p.tool == tool && p.host == host) return &p;
    return nullptr;
}

QString DownloadPins::sha256Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

}  // namespace ps2r::launcher
