// app/launcher/bundled_prepare_runner.h -- the PrepareRunner over the pipeline the engine ships (tools/prepare,
// ps2r_prepare): the release index names the engine bundle and the compiler for this computer, the pipeline turns the
// chosen disc into the game inside the game folder. Run() is synchronous, so it runs on a worker thread here and every
// Listener call is queued back to the Qt thread as the PrepareRunner signals. The pipeline's phase and failure texts are
// the only words shown (tools/prepare/ui_strings.h); its own log is what Details opens.
//
// The release index comes from the manifest (`release.index_url`, default the GitHub Releases API of `release.repo`);
// setIndexUrl() overrides it (the launcher's `--index`, which the cold-start judge uses with its offline fixture).
#pragma once
#include <memory>
#include "prepare_runner.h"
namespace ps2r::launcher {
class TitleManifest;

class BundledPrepareRunner : public PrepareRunner {
    Q_OBJECT
public:
    explicit BundledPrepareRunner(const TitleManifest* manifest, QObject* parent = nullptr);
    ~BundledPrepareRunner() override;
    bool available(const QString& targetId, QString* why) const override;
    QString estimateText(const QString& targetId) const override;
    bool start(const PrepareRequest& request, QString* error) override;
    void cancel() override;
    bool running() const override;
    QString logPath() const override;

    // Overrides for a scripted run (the headless `--prepare` mode): the index, the release tag, the job count.
    void setIndexUrl(const QString& url);
    void setReleaseTag(const QString& tag);
    void setJobs(int jobs);
    QString indexUrl() const;
    // After a run: the pipeline's report (phase seconds, bytes downloaded, cache hit, ...) in the same shape the
    // command-line tool writes, so one judge reads both.
    QString reportJson() const;
    bool lastOk() const;
    // Blocks until the worker thread is gone (after failed()/finished(); the headless mode calls it before the report).
    void waitFinished();
    // The pipeline's platform id of a manifest target: "<os>-<arch>" (macos-arm64, windows-x86_64, ...).
    static QString platformFor(const TitleManifest& manifest, const QString& targetId);
    static QString hostPlatform();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace ps2r::launcher
