// app/launcher/disc_identity.h -- what a disc image IS, decided by its content against the manifest: the supported
// disc (its executable is on it and its checksum matches), a recognised disc of another region (the badge "Not
// supported yet" with the manifest's reason), a modified image (the right executable, a different checksum), not the
// game, not a disc image, or unreadable. Every card and every error state is built from one of these; nothing is
// refused silently.
#pragma once
#include <QString>
namespace ps2r::launcher {
struct DiscIdentity {
    enum Status { Supported, OtherRegion, Modified, NotTheGame, NotAnIso, Unreadable };
    Status status = Unreadable;
    QString path;
    QString fingerprintFile;   // the manifest fingerprint that matched (Supported / OtherRegion / Modified)
    QString region, regionLabel, id;
    QString sha256;            // the executable's checksum as read from the image
    QString volumeId;
    QString reason;            // plain text for the badge / the error state
    qint64 sizeBytes = 0;

    bool recognised() const { return status == Supported || status == OtherRegion || status == Modified; }
    bool playable() const { return status == Supported; }
    static QString statusKey(Status s);   // "supported" / "other_region" / ... (persisted in launcher.json)
    static Status statusFromKey(const QString& key);
};

}  // namespace ps2r::launcher
