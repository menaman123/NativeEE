// app/launcher/title_manifest.h -- the per-title manifest (`title.json`) a title ships beside the launcher: who the
// game is (id, name, the disc's fingerprint), where its source is pinned, how each target is built, how it is run,
// and the settings rows that mirror its options file. Everything the launcher knows about a game comes from here.
#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
namespace ps2r::launcher {

struct DiscFingerprint {
    QString file;     // the file that must be on the disc (ISO 9660 root): the game's executable name
    QString region;   // PAL / NTSC-U -- shown on the card
    QString id;       // the disc id shown on the card
};

struct SettingsChoice {
    QString value;    // what is written to the options file
    QString label;    // what the user reads
};

struct SettingsRowSpec {
    QString key;      // the options-file key (e.g. video.frame_rate)
    QString label;
    QString kind;     // "choice" or "switch"
    QString defaultValue;
    QString help;
    QVector<SettingsChoice> choices;
};

struct SettingsGroupSpec {
    QString title;
    QVector<SettingsRowSpec> rows;
};

struct TargetSpec {
    QString id;             // macos / windows / vita
    QString label;
    QString host;           // the host that can build it: macos / windows / any
    QString artifact;       // binary / zip / vpk
    QString preset;         // a CMake configure preset, or empty
    QStringList configure;  // configure arguments when there is no preset (${ffmpeg} substituted)
    QString buildTarget;
    QString binary;         // the runnable binary under the build directory (macos / windows)
    QString artifactPath;   // the artefact under the build directory (vpk)
    QStringList needs;      // extra installs: vitasdk
    QString installText;    // how to put the artefact on that machine
    QString crossReason;    // why this host cannot build it
    QString unavailableReason;   // why NO host can build it from this source (a lane not merged yet) -- greyed with the reason
    QString distScript;     // a staging script run after the build (windows: release_dist.ps1)
    QStringList distArgs;
    QString distArtifact;   // the staged artefact
};

struct FolderSpec {
    QString id, label, defaultPath, seedFromSource;
};

struct LicenceLink { QString label, path; };

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
    // Every fingerprint file the launcher accepts (the main one first).
    QVector<DiscFingerprint> allFingerprints() const;

    QString sourceUrl() const { return sourceUrl_; }
    // The GitHub repository whose releases carry the finished game ("owner/name"): manifest release.repo, else the
    // source URL's github.com path. Empty when neither names one.
    QString releaseRepo() const { return releaseRepo_; }
    QString toolchainScript() const { return toolchainScript_; }
    QString toolchainWindowsScript() const { return toolchainWindowsScript_; }
    QString toolchainPins() const { return toolchainPins_; }
    QString toolchainFolder() const { return toolchainFolder_; }
    QString ffmpegRecipe() const { return ffmpegRecipe_; }
    bool needsPython() const { return needsPython_; }

    const QVector<TargetSpec>& targets() const { return targets_; }
    const TargetSpec* target(const QString& id) const;

    QString runIsoFlag() const { return runIsoFlag_; }
    QString runConfigFlag() const { return runConfigFlag_; }
    QString runMemoryCardsFlag() const { return runMemoryCardsFlag_; }
    QStringList runArgs() const { return runArgs_; }
    QString titleStateLine() const { return titleStateLine_; }
    QString titleStateName() const { return titleStateName_; }

    QString settingsFile() const { return settingsFile_; }
    QString settingsNote() const { return settingsNote_; }
    const QVector<SettingsGroupSpec>& settingsGroups() const { return settingsGroups_; }
    QVector<SettingsRowSpec> allSettingsRows() const;

    const QVector<FolderSpec>& folders() const { return folders_; }
    QStringList scanFolders() const { return scanFolders_; }
    bool scanRemovable() const { return scanRemovable_; }
    int scanMaxDepth() const { return scanMaxDepth_; }
    const QVector<LicenceLink>& licences() const { return licences_; }

private:
    bool valid_ = false;
    QString path_, id_, name_, product_, description_, appDataFolder_, binaryName_;
    DiscFingerprint fingerprint_;
    QVector<DiscFingerprint> alternates_;
    QStringList discExtensions_;
    QString releaseRepo_;
    QString sourceUrl_, toolchainScript_, toolchainWindowsScript_, toolchainPins_, toolchainFolder_, ffmpegRecipe_;
    bool needsPython_ = true;
    QVector<TargetSpec> targets_;
    QString runIsoFlag_, runConfigFlag_, runMemoryCardsFlag_, titleStateLine_, titleStateName_;
    QStringList runArgs_;
    QString settingsFile_, settingsNote_;
    QVector<SettingsGroupSpec> settingsGroups_;
    QVector<FolderSpec> folders_;
    QStringList scanFolders_;
    bool scanRemovable_ = true;
    int scanMaxDepth_ = 2;
    QVector<LicenceLink> licences_;
};

}  // namespace ps2r::launcher
