// app/launcher/download_pins.h -- the pinned tools the launcher itself installs (portable CMake, Ninja, Python when
// the machine has none): one row per tool per host with the URL, the archive name, the sha256 and the path of the
// executable inside the unpacked archive. A download whose sha256 does not match is deleted and reported.
#pragma once
#include <QString>
#include <QVector>
namespace ps2r::launcher {

struct DownloadPin {
    QString tool;        // cmake / ninja / python
    QString host;        // macos-arm64 / macos-x86_64 / windows-x86_64 / linux-x86_64
    QString version;
    QString url;
    QString archive;     // the file name under Downloads/
    QString sha256;
    QString executable;  // relative path of the tool inside the unpacked folder
    qint64 approxBytes;  // the download size shown before it starts
};

class DownloadPins {
public:
    static QString currentHost();
    static const QVector<DownloadPin>& all();
    static const DownloadPin* find(const QString& tool, const QString& host = currentHost());
    // The sha256 of a file, lower-case hex; empty when unreadable.
    static QString sha256Of(const QString& path);
};

}  // namespace ps2r::launcher
