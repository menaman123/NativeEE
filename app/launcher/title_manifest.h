// app/launcher/title_manifest.h -- the per-title manifest (`title.json`) a title ships beside the launcher: who the
// game is (id, name, the disc's fingerprint and the executable's checksum), which discs are recognised but not
// supported and why, which platforms it is prepared for, how the game folder is laid out, how the game is run, the
// settings rows that mirror its options file, and what is not supported. Everything the launcher knows about a game
// comes from here; nothing of a title is compiled in.
#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
namespace ps2r::launcher {

struct DiscFingerprint {
    QString file;         // the file that must be on the disc (ISO 9660 root): the game's executable name
    QString region;       // NTSC-U / PAL / NTSC-J -- shown on the card
    QString regionLabel;  // "North America" -- the plain name shown beside the region code
    QString id;           // the disc id shown on the card
    QString sha256;       // the executable's checksum (lower-case hex); empty = not checked
    bool supported = true;   // false = recognised, shown with the "Not supported yet" badge and `reason`
    QString reason;
};

struct SettingsChoice {
    QString value;         // what is written to the options file
    QString label;         // what the user reads
    QStringList platforms;   // empty = everywhere; else the host OS ids this choice is offered on (macos / windows / linux)
};

struct SettingsLock {
    QString key;     // another row's key
    QString value;   // while that row has this value, this row is locked...
    QString help;    // ...and says why
};

struct SettingsRowSpec {
    QString key;      // the options-file key (e.g. video.frame_cap)
    QString label;
    QString kind;     // "choice" or "switch"
    QString defaultValue;
    QString help;
    QVector<SettingsChoice> choices;
    SettingsLock lockedBy;   // key empty = no lock
};

struct SettingsGroupSpec {
    QString title;
    QVector<SettingsRowSpec> rows;
};

struct TargetSpec {
    QString id;       // macos-arm64 / windows-x64 / linux-x86_64 ...
    QString label;    // "macOS (Apple silicon)"
    QString os;       // macos / windows / linux
    QString arch;     // arm64 / x86_64
    QString binary;   // the game's executable name on that platform (sly2.app / sly2.exe / sly2)
};

struct UnsupportedItem { QString label, reason; };

struct SubfolderSpec { QString id, label, defaultName; };

struct LicenceLink { QString label, file; };

class TitleManifest {
public:
    bool load(const QString& path, QString* error);
    bool loadJson(const QJsonObject& root, QString* error);
    bool valid() const { return valid_; }

    QString path() const { return path_; }
    QString id() const { return id_; }
    QString name() const { return name_; }
    QString product() const { return product_; }
    QString description() const { return description_; }
    QString appDataFolder() const { return appDataFolder_; }
    QString binaryName() const { return binaryName_; }

    const DiscFingerprint& fingerprint() const { return fingerprint_; }
    const QVector<DiscFingerprint>& alternates() const { return alternates_; }
    QStringList discExtensions() const { return discExtensions_; }
    QStringList requiredFiles() const { return requiredFiles_; }
    QString modifiedReason() const { return modifiedReason_; }
    // Every fingerprint the launcher recognises (the supported one first).
    QVector<DiscFingerprint> allFingerprints() const;
    const DiscFingerprint* fingerprintFor(const QString& file) const;

    QString releaseRepo() const { return releaseRepo_; }
    // The release asset for a platform: release.asset_pattern with ${product}, ${tag}, ${platform} substituted.
    QString releaseAsset(const QString& tag, const QString& platform) const;
    QString bundleFolder(const QString& platform) const;

    const QVector<TargetSpec>& targets() const { return targets_; }
    const TargetSpec* target(const QString& id) const;
    const QVector<UnsupportedItem>& unsupported() const { return unsupported_; }

    QString configFileName() const { return configFileName_; }
    const QVector<SubfolderSpec>& subfolders() const { return subfolders_; }
    QString portableNote() const { return portableNote_; }

    // run.args with ${iso} ${config} ${memcards} ${saves} ${cache} ${folder} left for GameFolder to substitute
    QStringList runArgs() const { return runArgs_; }
    QStringList resumeArgs() const { return resumeArgs_; }
    QString saveStatesFolder() const { return saveStatesFolder_; }
    QString saveStateExtension() const { return saveStateExtension_; }
    QString saveStateMetaExtension() const { return saveStateMetaExtension_; }
    QString saveStateThumbnailExtension() const { return saveStateThumbnailExtension_; }

    QString settingsNote() const { return settingsNote_; }
    const QVector<SettingsGroupSpec>& settingsGroups() const { return settingsGroups_; }
    QVector<SettingsRowSpec> allSettingsRows() const;

    QStringList scanFolders() const { return scanFolders_; }
    bool scanRemovable() const { return scanRemovable_; }
    int scanMaxDepth() const { return scanMaxDepth_; }
    const QVector<LicenceLink>& licences() const { return licences_; }

private:
    bool valid_ = false;
    QString path_, id_, name_, product_, description_, appDataFolder_, binaryName_;
    DiscFingerprint fingerprint_;
    QVector<DiscFingerprint> alternates_;
    QStringList discExtensions_, requiredFiles_;
    QString modifiedReason_;
    QString releaseRepo_, assetPattern_, bundleFolderPattern_;
    QVector<TargetSpec> targets_;
    QVector<UnsupportedItem> unsupported_;
    QString configFileName_, portableNote_;
    QVector<SubfolderSpec> subfolders_;
    QStringList runArgs_, resumeArgs_;
    QString saveStatesFolder_, saveStateExtension_, saveStateMetaExtension_, saveStateThumbnailExtension_;
    QString settingsNote_;
    QVector<SettingsGroupSpec> settingsGroups_;
    QStringList scanFolders_;
    bool scanRemovable_ = true;
    int scanMaxDepth_ = 2;
    QVector<LicenceLink> licences_;
};

}  // namespace ps2r::launcher
