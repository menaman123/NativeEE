// app/launcher/disc_identifier.cpp -- the classification.
#include "disc_identifier.h"
#include <QFileInfo>
#include "iso_fingerprint.h"
#include "title_manifest.h"
namespace ps2r::launcher {

DiscIdentity DiscIdentifier::identify(const TitleManifest& manifest, const QString& path) {
    DiscIdentity d;
    d.path = path;
    d.sizeBytes = QFileInfo(path).size();
    QVector<IsoRootEntry> records;
    QString err;
    if (!IsoFingerprint::rootRecords(path, &records, &err)) {
        d.status = err == QStringLiteral("cannot open the file") ? DiscIdentity::Unreadable : DiscIdentity::NotAnIso;
        d.reason = d.status == DiscIdentity::Unreadable
                       ? QStringLiteral("\"%1\" could not be opened (%2).").arg(QFileInfo(path).fileName(), err)
                       : QStringLiteral("\"%1\" cannot be read as a disc image (%2). It may be a damaged download or a different format "
                                        "(a .bin with a .cue is not supported yet).").arg(QFileInfo(path).fileName(), err);
        return d;
    }
    QStringList names;
    for (const IsoRootEntry& e : records) names << e.name;
    const DiscFingerprint* fp = nullptr;
    for (const DiscFingerprint& f : manifest.allFingerprints()) {
        if (names.contains(f.file.toUpper())) { fp = manifest.fingerprintFor(f.file); break; }
    }
    d.volumeId = IsoFingerprint::volumeId(path);
    if (!fp) {
        QStringList wanted;
        for (const DiscFingerprint& f : manifest.allFingerprints()) wanted << f.file;
        d.status = DiscIdentity::NotTheGame;
        d.reason = QStringLiteral("This is not a %1 disc image. \"%2\" has no %3 on it. Choose a disc image of the game you own.")
                       .arg(manifest.name(), QFileInfo(path).fileName(), wanted.join(QStringLiteral(" or ")));
        return d;
    }
    for (const QString& required : manifest.requiredFiles()) {
        if (!names.contains(required.toUpper())) {
            d.status = DiscIdentity::Modified;
            d.fingerprintFile = fp->file;
            d.region = fp->region; d.regionLabel = fp->regionLabel; d.id = fp->id;
            d.reason = QStringLiteral("%1 is missing from the disc. ").arg(required) + manifest.modifiedReason();
            return d;
        }
    }
    d.fingerprintFile = fp->file;
    d.region = fp->region;
    d.regionLabel = fp->regionLabel;
    d.id = fp->id;
    if (!fp->supported) {
        d.status = DiscIdentity::OtherRegion;
        d.reason = fp->reason.isEmpty() ? QStringLiteral("This version prepares only the %1 disc (%2).").arg(manifest.fingerprint().regionLabel, manifest.fingerprint().id) : fp->reason;
        return d;
    }
    d.sha256 = IsoFingerprint::fileSha256(path, fp->file, &err);
    if (d.sha256.isEmpty()) {
        d.status = DiscIdentity::Modified;
        d.reason = QStringLiteral("%1 could not be read from the disc (%2). ").arg(fp->file, err) + manifest.modifiedReason();
        return d;
    }
    if (!fp->sha256.isEmpty() && d.sha256 != fp->sha256) {
        d.status = DiscIdentity::Modified;
        d.reason = manifest.modifiedReason();
        return d;
    }
    d.status = DiscIdentity::Supported;
    return d;
}

QString DiscIdentifier::subtitle(const DiscIdentity& d) {
    QStringList parts;
    if (!d.region.isEmpty()) parts << (d.regionLabel.isEmpty() ? d.region : QStringLiteral("%1 (%2)").arg(d.region, d.regionLabel));
    if (!d.id.isEmpty()) parts << d.id;
    parts << QFileInfo(d.path).fileName();
    return parts.join(QStringLiteral(" · "));
}

}  // namespace ps2r::launcher
