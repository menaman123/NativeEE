// app/launcher/bundled_prepare_runner.cpp -- the runner over tools/prepare on a worker thread.
#include "bundled_prepare_runner.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include "host_platform.h"
#include "title_manifest.h"
#include "tools/prepare/prepare.h"
namespace ps2r::launcher {

struct BundledPrepareRunner::Impl {
    // The pipeline calls these on its own thread; each one is queued to the Qt thread as a signal of the owner.
    struct Listener : ps2r::prepare::Listener {
        Listener(BundledPrepareRunner* owner, std::atomic<bool>* running) : owner_(owner), running_(running) {}
        void progress(double fraction, double eta_seconds, const std::string& phase_text) override {
            const QString text = QString::fromStdString(phase_text);
            const qint64 eta = eta_seconds > 0 ? qint64(eta_seconds + 0.5) : -1;
            QPointer<BundledPrepareRunner> owner = owner_;
            QMetaObject::invokeMethod(owner_, [owner, fraction, eta, text] { if (owner) emit owner->progress(fraction, eta, text); }, Qt::QueuedConnection);
        }
        void failed(const std::string& text, const std::string& log_path) override {
            running_->store(false);   // before the signal: a receiver asking running() during failed() hears the truth
            const QString t = QString::fromStdString(text), log = QString::fromStdString(log_path);
            QPointer<BundledPrepareRunner> owner = owner_;
            QMetaObject::invokeMethod(owner_, [owner, t, log] { if (owner) emit owner->failed(t, log); }, Qt::QueuedConnection);
        }
        void finished(const std::string& binary_path) override {
            running_->store(false);
            const QString b = QString::fromStdString(binary_path);
            QPointer<BundledPrepareRunner> owner = owner_;
            QMetaObject::invokeMethod(owner_, [owner, b] { if (owner) emit owner->finished(b); }, Qt::QueuedConnection);
        }
        BundledPrepareRunner* owner_;
        std::atomic<bool>* running_;
    };

    const TitleManifest* manifest = nullptr;
    QString indexOverride, releaseTag;
    int jobs = 0;
    PrepareRequest request;
    std::atomic<bool> running{false};
    std::unique_ptr<Listener> listener;
    std::unique_ptr<ps2r::prepare::PrepareRunner> runner;
    std::thread worker;
    std::mutex reportMutex;
    ps2r::prepare::Report report;
    bool lastOk = false;
};

namespace {
void joinWorker(BundledPrepareRunner::Impl& impl) { if (impl.worker.joinable()) impl.worker.join(); }
}  // namespace

BundledPrepareRunner::BundledPrepareRunner(const TitleManifest* manifest, QObject* parent) : PrepareRunner(parent), impl_(new Impl) {
    impl_->manifest = manifest;
}

BundledPrepareRunner::~BundledPrepareRunner() {
    if (impl_->runner) impl_->runner->Cancel();
    joinWorker(*impl_);
}

QString BundledPrepareRunner::hostPlatform() { return QString::fromStdString(ps2r::prepare::PrepareRunner::HostPlatform()); }

QString BundledPrepareRunner::platformFor(const TitleManifest& manifest, const QString& targetId) {
    const TargetSpec* t = manifest.target(targetId);
    if (!t) return hostPlatform();
    return t->os + "-" + t->arch;
}

bool BundledPrepareRunner::available(const QString& targetId, QString* why) const {
    // The pipeline runs this computer's compiler; it cannot make the game for another kind of computer from here.
    const QString host = HostPlatform::targetId(*impl_->manifest);
    if (!targetId.isEmpty() && targetId != host) {
        if (why) *why = QStringLiteral("The game can only be made on the computer it is for. Open the launcher on that computer and it is made there.");
        return false;
    }
    return true;
}

QString BundledPrepareRunner::estimateText(const QString&) const {
    const QString e = impl_->manifest->prepareEstimate();
    return e.isEmpty() ? QStringLiteral("about 1 to 2 minutes") : e;
}

void BundledPrepareRunner::setIndexUrl(const QString& url) { impl_->indexOverride = url; }
void BundledPrepareRunner::setReleaseTag(const QString& tag) { impl_->releaseTag = tag; }
void BundledPrepareRunner::setJobs(int jobs) { impl_->jobs = jobs; }

QString BundledPrepareRunner::indexUrl() const {
    // only a named index (--index): the launcher never goes online by itself; a release ships the game beside it
    return impl_->indexOverride;
}

bool BundledPrepareRunner::running() const { return impl_->running.load(); }

QString BundledPrepareRunner::logPath() const {
    // the pipeline's own log, inside the game folder's cache; the launcher's log only says where it is
    if (!impl_->request.gameFolder.isEmpty()) {
        const QString p = impl_->request.gameFolder + "/cache/logs/prepare.log";
        if (QFileInfo::exists(p)) return p;
    }
    return impl_->request.logPath;
}

bool BundledPrepareRunner::start(const PrepareRequest& request, QString* error) {
    if (impl_->running.load()) { if (error) *error = QStringLiteral("The game is already being prepared."); return false; }
    QString why;
    if (!available(request.targetId, &why)) { if (error) *error = why; return false; }
    if (indexUrl().isEmpty()) {
        if (error) *error = QStringLiteral("This launcher does not know where to get the files your game needs. Download the complete release and open the launcher from it.");
        return false;
    }
    joinWorker(*impl_);
    impl_->request = request;
    ps2r::prepare::Options o;
    o.index_url = indexUrl().toStdString();
    o.release_tag = impl_->releaseTag.toStdString();
    o.platform = platformFor(*impl_->manifest, request.targetId).toStdString();
    o.iso = request.isoPath.toStdString();
    o.folder = request.gameFolder.toStdString();
    o.jobs = impl_->jobs;
    QDir().mkpath(QFileInfo(request.logPath).absolutePath());
    QFile launcherLog(request.logPath);
    if (launcherLog.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        launcherLog.write(QStringLiteral("disc: %1\nfolder: %2\nplatform: %3\nindex: %4\nlog: %5/cache/logs/prepare.log\n")
                              .arg(request.isoPath, request.gameFolder, QString::fromStdString(o.platform), indexUrl(), request.gameFolder).toUtf8());
    }
    impl_->listener.reset(new Impl::Listener(this, &impl_->running));
    impl_->runner.reset(new ps2r::prepare::PrepareRunner(o, *impl_->listener));
    impl_->running.store(true);
    impl_->lastOk = false;
    ps2r::prepare::PrepareRunner* runner = impl_->runner.get();
    Impl* impl = impl_.get();
    impl_->worker = std::thread([runner, impl] {
        const bool ok = runner->Run();
        std::lock_guard<std::mutex> lock(impl->reportMutex);
        impl->report = runner->report();
        impl->lastOk = ok;
        impl->running.store(false);
    });
    return true;
}

void BundledPrepareRunner::cancel() {
    if (impl_->runner) impl_->runner->Cancel();
}

void BundledPrepareRunner::waitFinished() { joinWorker(*impl_); }

bool BundledPrepareRunner::lastOk() const {
    std::lock_guard<std::mutex> lock(impl_->reportMutex);
    return impl_->lastOk;
}

QString BundledPrepareRunner::reportJson() const {
    std::lock_guard<std::mutex> lock(impl_->reportMutex);
    const ps2r::prepare::Report& r = impl_->report;
    auto s = [](const std::string& v) { return QString::fromStdString(v); };
    QJsonObject phases;
    for (const auto& kv : r.phase_seconds) phases[s(kv.first)] = kv.second;
    QJsonObject j;
    j["ok"] = impl_->lastOk;
    j["release_tag"] = s(r.release_tag);
    j["platform"] = s(r.platform);
    j["bundle_dir"] = s(r.bundle_dir);
    j["compiler_dir"] = s(r.compiler_dir);   // lint: allow
    j["compiler_driver"] = s(r.compiler_driver);   // lint: allow
    j["sysroot"] = s(r.sysroot);
    j["binary_path"] = s(r.binary_path);
    j["ini_path"] = s(r.ini_path);
    j["log_path"] = s(r.log_path);
    j["work_dir"] = s(r.work_dir);
    j["elf_sha256"] = s(r.elf_sha256);   // lint: allow
    j["build_id"] = s(r.build_id);
    j["vu1_aot_abi_key"] = s(r.abi_key);
    j["bytes_downloaded"] = double(r.bytes_downloaded);
    j["bytes_total"] = double(r.bytes_total);
    j["cache_hit"] = r.cache_hit;
    j["manifest_ok"] = r.manifest_ok;
    j["phase_seconds"] = phases;
    j["total_seconds"] = r.total_seconds;
    j["failed_phase"] = QString::fromLatin1(ps2r::prepare::PhaseName(r.failed_phase));
    j["failure"] = s(r.failure);
    j["driver"] = QStringLiteral("launcher");
    return QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Indented));
}

}  // namespace ps2r::launcher
