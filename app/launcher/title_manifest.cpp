// app/launcher/title_manifest.cpp -- reading title.json.
#include "title_manifest.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
namespace ps2r::launcher {

namespace {
QStringList stringList(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& e : v.toArray()) out << e.toString();
    return out;
}
DiscFingerprint fingerprintOf(const QJsonObject& o, bool supportedDefault) {
    DiscFingerprint f;
    f.file = o.value("file").toString();
    f.region = o.value("region").toString();
    f.regionLabel = o.value("region_label").toString();
    f.id = o.value("id").toString();
    f.sha256 = o.value("sha256").toString().toLower();   // lint: allow
    f.supported = o.value("supported").toBool(supportedDefault);
    f.reason = o.value("reason").toString();
    return f;
}
}  // namespace

bool TitleManifest::load(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("%1 is not a game description: %2").arg(path, perr.errorString());
        return false;
    }
    path_ = path;
    return loadJson(doc.object(), error);
}

bool TitleManifest::loadJson(const QJsonObject& root, QString* error) {
    valid_ = false;
    id_ = root.value("id").toString();
    name_ = root.value("name").toString();
    product_ = root.value("product").toString(name_);
    description_ = root.value("description").toString();
    appDataFolder_ = root.value("app_data_folder").toString(product_);
    binaryName_ = root.value("binary_name").toString(product_.toLower());
    if (id_.isEmpty() || name_.isEmpty()) {
        if (error) *error = QStringLiteral("the game description needs an id and a name");
        return false;
    }
    const QJsonObject disc = root.value("disc").toObject();
    fingerprint_ = fingerprintOf(disc.value("fingerprint").toObject(), true);
    if (fingerprint_.file.isEmpty()) {
        if (error) *error = QStringLiteral("the game description needs disc.fingerprint.file");
        return false;
    }
    alternates_.clear();
    for (const QJsonValue& v : disc.value("alternates").toArray()) alternates_ << fingerprintOf(v.toObject(), false);
    discExtensions_ = stringList(disc.value("extensions"));
    if (discExtensions_.isEmpty()) discExtensions_ << ".iso";
    requiredFiles_ = stringList(disc.value("required_files"));
    modifiedReason_ = disc.value("modified_reason").toString(
        QStringLiteral("This disc image carries the right game but its code differs from the original disc, so it cannot be prepared."));

    const QJsonObject release = root.value("release").toObject();
    releaseRepo_ = release.value("repo").toString();
    assetPattern_ = release.value("asset_pattern").toString("${product}-${tag}-${platform}.zip");
    bundleFolderPattern_ = release.value("bundle_folder").toString("${product}-${platform}");

    targets_.clear();
    const QJsonObject targets = root.value("targets").toObject();
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        TargetSpec t;
        t.id = it.key();
        t.label = o.value("label").toString(t.id);
        t.os = o.value("os").toString(t.id.section('-', 0, 0));
        t.arch = o.value("arch").toString(t.id.section('-', 1));
        t.binary = o.value("binary").toString(binaryName_);
        targets_ << t;
    }
    // a stable order: macOS, Windows, Linux, then anything else alphabetically
    const QStringList order{"macos", "windows", "linux"};
    std::sort(targets_.begin(), targets_.end(), [&](const TargetSpec& a, const TargetSpec& b) {
        const int ia = order.indexOf(a.os), ib = order.indexOf(b.os);
        if (ia != ib) return (ia < 0 ? 99 : ia) < (ib < 0 ? 99 : ib);
        return a.id < b.id;
    });
    unsupported_.clear();
    for (const QJsonValue& v : root.value("unsupported").toArray()) {
        const QJsonObject o = v.toObject();
        unsupported_ << UnsupportedItem{o.value("label").toString(), o.value("reason").toString()};
    }

    const QJsonObject folder = root.value("game_folder").toObject();
    configFileName_ = folder.value("config").toString("nativeee.ini");
    portableNote_ = folder.value("portable_note").toString();
    subfolders_.clear();
    const QJsonObject subs = folder.value("subfolders").toObject();
    for (const QString& id : {QStringLiteral("memcards"), QStringLiteral("saves"), QStringLiteral("cache")}) {
        const QJsonObject o = subs.value(id).toObject();
        SubfolderSpec s;
        s.id = id;
        s.label = o.value("label").toString(id);
        s.defaultName = o.value("default").toString(id);
        subfolders_ << s;
    }
    for (auto it = subs.begin(); it != subs.end(); ++it) {   // any extra sub-folder the title names
        bool known = false;
        for (const SubfolderSpec& s : subfolders_) known = known || s.id == it.key();
        if (known) continue;
        const QJsonObject o = it.value().toObject();
        subfolders_ << SubfolderSpec{it.key(), o.value("label").toString(it.key()), o.value("default").toString(it.key())};
    }

    const QJsonObject run = root.value("run").toObject();
    runArgs_ = stringList(run.value("args"));
    if (runArgs_.isEmpty()) runArgs_ << "--iso" << "${iso}" << "--config" << "${config}";
    resumeArgs_ = stringList(run.value("resume_args"));
    const QJsonObject states = run.value("save_states").toObject();
    saveStatesFolder_ = states.value("folder").toString("${saves}");
    saveStateExtension_ = states.value("extension").toString(".ps2st");
    saveStateMetaExtension_ = states.value("meta_extension").toString(".meta");
    saveStateThumbnailExtension_ = states.value("thumbnail_extension").toString(".png");

    const QJsonObject settings = root.value("settings").toObject();
    settingsNote_ = settings.value("note").toString();
    settingsGroups_.clear();
    for (const QJsonValue& gv : settings.value("groups").toArray()) {
        const QJsonObject g = gv.toObject();
        SettingsGroupSpec group;
        group.title = g.value("title").toString();
        for (const QJsonValue& rv : g.value("rows").toArray()) {
            const QJsonObject r = rv.toObject();
            SettingsRowSpec row;
            row.key = r.value("key").toString();
            row.label = r.value("label").toString(row.key);
            row.kind = r.value("kind").toString("choice");
            row.defaultValue = r.value("default").toString();
            row.help = r.value("help").toString();
            for (const QJsonValue& cv : r.value("choices").toArray()) {
                const QJsonObject c = cv.toObject();
                row.choices << SettingsChoice{c.value("value").toString(), c.value("label").toString(c.value("value").toString()),
                                              stringList(c.value("platforms"))};
            }
            if (row.kind == "switch" && row.choices.isEmpty()) row.choices << SettingsChoice{"off", "Off", {}} << SettingsChoice{"on", "On", {}};
            const QJsonObject lock = r.value("locked_by").toObject();
            row.lockedBy = SettingsLock{lock.value("key").toString(), lock.value("value").toString(), lock.value("help").toString()};
            if (!row.key.isEmpty()) group.rows << row;
        }
        settingsGroups_ << group;
    }

    const QJsonObject scan = root.value("scan").toObject();
    scanFolders_ = stringList(scan.value("folders"));
    if (scanFolders_.isEmpty()) scanFolders_ << "~/Downloads" << "~/Desktop" << "~/Documents";
    scanRemovable_ = scan.value("removable").toBool(true);
    scanMaxDepth_ = scan.value("max_depth").toInt(2);
    licences_.clear();
    for (const QJsonValue& v : root.value("licences").toArray()) {
        const QJsonObject o = v.toObject();
        licences_ << LicenceLink{o.value("label").toString(), o.value("file").toString(o.value("path").toString())};
    }
    valid_ = true;
    return true;
}

QVector<DiscFingerprint> TitleManifest::allFingerprints() const {
    QVector<DiscFingerprint> all;
    all << fingerprint_;
    all += alternates_;
    return all;
}

const DiscFingerprint* TitleManifest::fingerprintFor(const QString& file) const {
    if (fingerprint_.file.compare(file, Qt::CaseInsensitive) == 0) return &fingerprint_;
    for (const DiscFingerprint& f : alternates_) if (f.file.compare(file, Qt::CaseInsensitive) == 0) return &f;
    return nullptr;
}

QString TitleManifest::releaseAsset(const QString& tag, const QString& platform) const {
    QString s = assetPattern_;
    s.replace("${product}", product_);
    s.replace("${tag}", tag);
    s.replace("${platform}", platform);
    return s;
}

QString TitleManifest::bundleFolder(const QString& platform) const {
    QString s = bundleFolderPattern_;
    s.replace("${product}", product_);
    s.replace("${platform}", platform);
    return s;
}

const TargetSpec* TitleManifest::target(const QString& id) const {
    for (const TargetSpec& t : targets_) if (t.id == id) return &t;
    return nullptr;
}

QVector<SettingsRowSpec> TitleManifest::allSettingsRows() const {
    QVector<SettingsRowSpec> rows;
    for (const SettingsGroupSpec& g : settingsGroups_) rows += g.rows;
    return rows;
}

}  // namespace ps2r::launcher
