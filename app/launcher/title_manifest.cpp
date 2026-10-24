// app/launcher/title_manifest.cpp -- reading title.json.
#include "title_manifest.h"
#include <QRegularExpression>
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
DiscFingerprint fingerprintOf(const QJsonObject& o) {
    DiscFingerprint f;
    f.file = o.value("file").toString();
    f.region = o.value("region").toString();
    f.id = o.value("id").toString();
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
        if (error) *error = QStringLiteral("%1 is not a JSON object: %2").arg(path, perr.errorString());
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
    binaryName_ = root.value("binary_name").toString();
    if (id_.isEmpty() || name_.isEmpty()) {
        if (error) *error = QStringLiteral("the manifest needs an id and a name");
        return false;
    }
    const QJsonObject disc = root.value("disc").toObject();
    fingerprint_ = fingerprintOf(disc.value("fingerprint").toObject());
    if (fingerprint_.file.isEmpty()) {
        if (error) *error = QStringLiteral("the manifest needs disc.fingerprint.file");
        return false;
    }
    alternates_.clear();
    for (const QJsonValue& v : disc.value("alternates").toArray()) alternates_ << fingerprintOf(v.toObject());
    discExtensions_ = stringList(disc.value("extensions"));
    if (discExtensions_.isEmpty()) discExtensions_ << ".iso";

    const QJsonObject source = root.value("source").toObject();
    sourceUrl_ = source.value("url").toString();
    releaseRepo_ = root.value("release").toObject().value("repo").toString();
    if (releaseRepo_.isEmpty()) {   // https://github.com/<owner>/<name>(.git)
        const QRegularExpressionMatch m = QRegularExpression("github\\.com[/:]([^/]+)/([^/.]+)").match(sourceUrl_);
        if (m.hasMatch()) releaseRepo_ = m.captured(1) + "/" + m.captured(2);
    }
    const QJsonObject tc = source.value("toolchain").toObject();
    toolchainScript_ = tc.value("script").toString("tools/fetch_toolchain.sh");
    toolchainWindowsScript_ = tc.value("windows_script").toString("tools/fetch_toolchain.ps1");
    toolchainPins_ = tc.value("pins").toString("tools/toolchain/PINS.sha256");
    toolchainFolder_ = tc.value("folder").toString("tools/toolchain");
    ffmpegRecipe_ = source.value("ffmpeg_recipe").toString();
    needsPython_ = source.value("needs_python").toBool(true);

    targets_.clear();
    const QJsonObject targets = root.value("targets").toObject();
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        TargetSpec t;
        t.id = it.key();
        t.label = o.value("label").toString(t.id);
        t.host = o.value("host").toString("any");
        t.artifact = o.value("artifact").toString("binary");
        t.preset = o.value("preset").toString();
        t.configure = stringList(o.value("configure"));
        t.buildTarget = o.value("build_target").toString();
        t.binary = o.value("binary").toString();
        t.artifactPath = o.value("artifact_path").toString();
        t.needs = stringList(o.value("needs"));
        t.installText = o.value("install_text").toString();
        t.crossReason = o.value("cross_reason").toString();
        t.unavailableReason = o.value("unavailable_reason").toString();
        const QJsonObject dist = o.value("dist").toObject();
        t.distScript = dist.value("script").toString();
        t.distArgs = stringList(dist.value("args"));
        t.distArtifact = dist.value("artifact").toString();
        targets_ << t;
    }
    // a stable order: macos, windows, vita, then anything else alphabetically
    const QStringList order{"macos", "windows", "vita"};
    std::sort(targets_.begin(), targets_.end(), [&](const TargetSpec& a, const TargetSpec& b) {
        const int ia = order.indexOf(a.id), ib = order.indexOf(b.id);
        if (ia >= 0 || ib >= 0) return (ia < 0 ? 99 : ia) < (ib < 0 ? 99 : ib);
        return a.id < b.id;
    });

    const QJsonObject run = root.value("run").toObject();
    runIsoFlag_ = run.value("iso_flag").toString("--iso");
    runConfigFlag_ = run.value("config_flag").toString("--config");
    runMemoryCardsFlag_ = run.value("memory_cards_flag").toString();
    runArgs_ = stringList(run.value("args"));
    titleStateLine_ = run.value("title_state_line").toString();
    titleStateName_ = run.value("title_state_name").toString();

    const QJsonObject settings = root.value("settings").toObject();
    settingsFile_ = settings.value("file").toString();
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
                row.choices << SettingsChoice{c.value("value").toString(), c.value("label").toString(c.value("value").toString())};
            }
            if (row.kind == "switch" && row.choices.isEmpty()) row.choices << SettingsChoice{"off", "Off"} << SettingsChoice{"on", "On"};
            if (!row.key.isEmpty()) group.rows << row;
        }
        settingsGroups_ << group;
    }

    folders_.clear();
    const QJsonObject folders = root.value("folders").toObject();
    for (const QString& id : {QStringLiteral("games"), QStringLiteral("mods"), QStringLiteral("memory_cards"), QStringLiteral("game_output")}) {
        if (!folders.contains(id)) continue;
        const QJsonObject o = folders.value(id).toObject();
        FolderSpec fs;
        fs.id = id;
        fs.label = o.value("label").toString(id);
        fs.defaultPath = o.value("default").toString();
        fs.seedFromSource = o.value("seed_from_source").toString();
        folders_ << fs;
    }
    const QJsonObject scan = root.value("scan").toObject();
    scanFolders_ = stringList(scan.value("folders"));
    scanRemovable_ = scan.value("removable").toBool(true);
    scanMaxDepth_ = scan.value("max_depth").toInt(2);
    licences_.clear();
    for (const QJsonValue& v : root.value("licences").toArray()) {
        const QJsonObject o = v.toObject();
        licences_ << LicenceLink{o.value("label").toString(), o.value("path").toString()};
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
