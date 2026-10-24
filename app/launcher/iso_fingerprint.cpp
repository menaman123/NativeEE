// app/launcher/iso_fingerprint.cpp -- the ISO 9660 root directory, read directly; the executable's checksum.
#include "iso_fingerprint.h"
#include <QCryptographicHash>
#include <QFile>
namespace ps2r::launcher {

namespace {
constexpr qint64 kSector = 2048;
constexpr qint64 kPvdSector = 16;
constexpr qint64 kMaxRootBytes = 64 * kSector;   // a root directory larger than this is not a game disc
constexpr qint64 kMaxHashedBytes = 64ll * 1024 * 1024;   // an "executable" larger than this is not one

quint32 le32(const QByteArray& b, int off) {
    if (off + 4 > b.size()) return 0;
    const uchar* p = reinterpret_cast<const uchar*>(b.constData()) + off;
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

bool readPvd(QFile& f, QByteArray* pvd, QString* error) {
    if (f.size() < (kPvdSector + 1) * kSector) {
        if (error) *error = QStringLiteral("too small to be a disc image");
        return false;
    }
    // Volume descriptors from sector 16: the primary one has type 1 and the "CD001" identifier.
    for (int s = 0; s < 16; ++s) {
        if (!f.seek((kPvdSector + s) * kSector)) break;
        const QByteArray d = f.read(kSector);
        if (d.size() < kSector) break;
        if (d.mid(1, 5) != "CD001") {
            if (s == 0) {
                if (error) *error = QStringLiteral("no ISO 9660 volume descriptor at sector 16");
                return false;
            }
            break;
        }
        const uchar type = uchar(d[0]);
        if (type == 1) { *pvd = d; return true; }
        if (type == 255) break;
    }
    if (error) *error = QStringLiteral("no primary volume descriptor");
    return false;
}
}  // namespace

bool IsoFingerprint::rootRecords(const QString& path, QVector<IsoRootEntry>* entries, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open the file");
        return false;
    }
    QByteArray pvd;
    if (!readPvd(f, &pvd, error)) return false;
    // the root directory record at byte 156 of the PVD: extent at +2 (LE), data length at +10 (LE)
    const quint32 rootLba = le32(pvd, 156 + 2);
    const quint32 rootLen = le32(pvd, 156 + 10);
    if (rootLba == 0 || rootLen == 0 || rootLen > kMaxRootBytes) {
        if (error) *error = QStringLiteral("the root directory record is damaged");
        return false;
    }
    if (!f.seek(qint64(rootLba) * kSector)) {
        if (error) *error = QStringLiteral("cannot read the root directory");
        return false;
    }
    const QByteArray dir = f.read(rootLen);
    if (dir.size() < int(rootLen)) {
        if (error) *error = QStringLiteral("the image ends before its root directory");
        return false;
    }
    entries->clear();
    int off = 0;
    while (off < dir.size()) {
        const int len = uchar(dir[off]);
        if (len == 0) {   // padding to the next sector
            off = ((off / int(kSector)) + 1) * int(kSector);
            continue;
        }
        if (off + len > dir.size() || len < 33) break;
        const int idLen = uchar(dir[off + 32]);
        QByteArray id = dir.mid(off + 33, idLen);
        if (!(idLen == 1 && (id[0] == '\0' || id[0] == '\1'))) {   // skip . and ..
            const int semi = id.indexOf(';');
            if (semi >= 0) id.truncate(semi);
            IsoRootEntry e;
            e.name = QString::fromLatin1(id).toUpper();
            e.lba = le32(dir, off + 2);
            e.length = le32(dir, off + 10);
            e.directory = (uchar(dir[off + 25]) & 0x02) != 0;
            *entries << e;
        }
        off += len;
    }
    return true;
}

bool IsoFingerprint::rootEntries(const QString& path, QStringList* entries, QString* error) {
    QVector<IsoRootEntry> records;
    if (!rootRecords(path, &records, error)) return false;
    entries->clear();
    for (const IsoRootEntry& e : records) *entries << e.name;
    return true;
}

IsoFingerprint::Result IsoFingerprint::identify(const QString& path, const QStringList& files, QString* matched, QString* error) {
    QStringList entries;
    QString err;
    if (!rootEntries(path, &entries, &err)) {
        if (error) *error = err;
        return err == QStringLiteral("cannot open the file") ? Unreadable : NotAnIso;
    }
    for (const QString& want : files) {
        if (entries.contains(want.toUpper())) {
            if (matched) *matched = want;
            return Match;
        }
    }
    if (error) *error = QStringLiteral("none of %1 is on the disc").arg(files.join(", "));
    return NotTheGame;
}

QString IsoFingerprint::fileSha256(const QString& path, const QString& name, QString* error) {
    QVector<IsoRootEntry> records;
    if (!rootRecords(path, &records, error)) return QString();
    const IsoRootEntry* hit = nullptr;
    for (const IsoRootEntry& e : records) if (!e.directory && e.name == name.toUpper()) { hit = &e; break; }
    if (!hit) {
        if (error) *error = QStringLiteral("%1 is not on the disc").arg(name);
        return QString();
    }
    if (hit->length == 0 || hit->length > kMaxHashedBytes) {
        if (error) *error = QStringLiteral("%1 has an impossible size").arg(name);
        return QString();
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(hit->lba) * kSector)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(name);
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 left = hit->length;
    while (left > 0) {
        const QByteArray chunk = f.read(qMin<qint64>(left, 1 << 20));
        if (chunk.isEmpty()) {
            if (error) *error = QStringLiteral("the image ends before %1 does").arg(name);
            return QString();
        }
        hash.addData(chunk);
        left -= chunk.size();
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString IsoFingerprint::volumeId(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QByteArray pvd;
    QString err;
    if (!readPvd(f, &pvd, &err)) return QString();
    return QString::fromLatin1(pvd.mid(40, 32)).trimmed();
}

}  // namespace ps2r::launcher
