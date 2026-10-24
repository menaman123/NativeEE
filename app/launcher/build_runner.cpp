// app/launcher/build_runner.cpp -- configure, build, stage, record.
#include "build_runner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include "app_data.h"
#include "host_tools.h"
#include "installer.h"
#include "process_runner.h"
namespace ps2r::launcher {

BuildRunner::BuildRunner(AppData* appData, const TitleManifest* manifest, Installer* installer, QObject* parent)
    : QObject(parent), appData_(appData), manifest_(manifest), installer_(installer) {
    proc_ = new ProcessRunner(this);
    connect(proc_, &ProcessRunner::line, this, &BuildRunner::logLine);
    connect(proc_, &ProcessRunner::progress, this, [this](int done, int total) {
        const qint64 now = timer_.elapsed();
        lastDone_ = done; lastTotal_ = total;
        recent_ << qMakePair(now, done);
        while (recent_.size() > 20) recent_.removeFirst();
        qint64 remaining = -1;
        if (recent_.size() >= 2) {
            const double steps = recent_.last().second - recent_.first().second;
            const double ms = recent_.last().first - recent_.first().first;
            if (steps > 0 && ms > 0) remaining = qint64((total - done) * (ms / steps) / 1000.0);
        }
        emit progress(QStringLiteral("Compiling the game (%1 of %2)").arg(done).arg(total), done, total, now / 1000, remaining);
    });
}

QString BuildRunner::hostTargetId() {
#ifdef Q_OS_MACOS
    return "macos";
#elif defined(Q_OS_WIN)
    return "windows";
#else
    return "linux";
#endif
}

bool BuildRunner::hostCanBuild(const TargetSpec& target, QString* reason) {
    if (!target.unavailableReason.isEmpty()) { if (reason) *reason = target.unavailableReason; return false; }
    if (target.host == "any" || target.host == hostTargetId()) return true;
    if (reason) *reason = target.crossReason.isEmpty() ? QStringLiteral("This target can only be built on %1.").arg(target.host) : target.crossReason;
    return false;
}

QString BuildRunner::buildDir(const QString& targetId) const { return appData_->buildDir(manifest_->id(), targetId); }

BuildRecord BuildRunner::record(const QString& targetId) const {
    BuildRecord r;
    QFile f(buildDir(targetId) + "/build.json");
    if (!f.open(QIODevice::ReadOnly)) return r;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    r.ok = o.value("ok").toBool();
    r.sourceCommit = o.value("source_commit").toString();
    r.toolchainPins = o.value("toolchain_pins").toString();
    r.builtAt = QDateTime::fromString(o.value("built_at").toString(), Qt::ISODate);
    r.binaryPath = o.value("binary").toString();
    r.artifactPath = o.value("artifact").toString();
    r.artifactBytes = qint64(o.value("artifact_bytes").toDouble());
    r.seconds = qint64(o.value("seconds").toDouble());
    r.failure = o.value("failure").toString();
    return r;
}

void BuildRunner::saveRecord(const QString& targetId, const BuildRecord& r) const {
    QDir().mkpath(buildDir(targetId));
    QJsonObject o;
    o["ok"] = r.ok;
    o["source_commit"] = r.sourceCommit;
    o["toolchain_pins"] = r.toolchainPins;
    o["built_at"] = r.builtAt.toString(Qt::ISODate);
    o["binary"] = r.binaryPath;
    o["artifact"] = r.artifactPath;
    o["artifact_bytes"] = double(r.artifactBytes);
    o["seconds"] = double(r.seconds);
    o["failure"] = r.failure;
    QSaveFile f(buildDir(targetId) + "/build.json");
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

bool BuildRunner::upToDate(const QString& targetId) const {
    const BuildRecord r = record(targetId);
    if (!r.ok) return false;
    if (r.sourceCommit != installer_->sourceCommit() || r.toolchainPins != installer_->toolchainPinsHash()) return false;
    return QFileInfo::exists(r.artifactPath);
}

QString BuildRunner::currentLogPath() const { return proc_->logPath(); }

QStringList BuildRunner::substitute(const QStringList& args) const {
    QStringList out;
    for (QString a : args) {
        a.replace("${ffmpeg}", installer_->ffmpegPrefix());
        a.replace("${build}", buildDir(target_));
        a.replace("${source}", installer_->sourceDir());
        a.replace("${vitasdk}", installer_->vitaSdkDir());
        out << a;
    }
    return out;
}

void BuildRunner::start(const QString& targetId) {
    if (running_) return;
    spec_ = manifest_->target(targetId);
    if (!spec_) return;
    QString reason;
    if (!hostCanBuild(*spec_, &reason)) {
        BuildRecord r; r.failure = reason;
        emit finished(targetId, false, r);
        return;
    }
    target_ = targetId;
    running_ = true;
    timer_.start();
    recent_.clear();
    lastDone_ = lastTotal_ = 0;
    QDir().mkpath(buildDir(targetId));
    configure();
}

void BuildRunner::cancel() { if (running_) proc_->cancel(); }

void BuildRunner::configure() {
    HostTools host(appData_);
    const QString cmake = host.cmake().isEmpty() ? HostTools::which("cmake") : host.cmake();
    if (cmake.isEmpty()) { finish(false, QStringLiteral("CMake is not installed (the first run did not finish).")); return; }
    QStringList args;
    const QString src = installer_->sourceDir();
    if (!spec_->preset.isEmpty()) args << "-S" << src << "--preset" << spec_->preset << "-B" << buildDir(target_);
    else args << "-S" << src << "-B" << buildDir(target_) << substitute(spec_->configure);
    if (!host.ninja().isEmpty()) args << "-DCMAKE_MAKE_PROGRAM=" + host.ninja();
    if (spec_->needs.contains("vitasdk")) args << "-DVITASDK=" + installer_->vitaSdkDir();
    emit progress(QStringLiteral("Preparing the build for %1").arg(spec_->label), 0, 0, timer_.elapsed() / 1000, -1);
    proc_->setLogPath(appData_->logPath("build-" + target_));
    proc_->setWorkingDirectory(src);
    QProcessEnvironment env = host.buildEnvironment();
#ifdef Q_OS_WIN
    env.insert("VCPKG_ROOT", QDir::toNativeSeparators(installer_->vcpkgRoot()));
#endif
    QStringList path;
    if (!host.cmake().isEmpty()) path << QFileInfo(host.cmake()).absolutePath();
    if (!host.ninja().isEmpty()) path << QFileInfo(host.ninja()).absolutePath();
    if (!host.git().isEmpty()) path << QFileInfo(host.git()).absolutePath();   // vcpkg and the scripts call git by name (the portable MinGit too)
    if (spec_->needs.contains("vitasdk")) { path << installer_->vitaSdkDir() + "/bin"; env.insert("VITASDK", installer_->vitaSdkDir()); }
    path << env.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    env.insert("PATH", path.join(QDir::listSeparator()));
    env.insert("HOME", AppData::homePath());
    env.insert("TE_MIN_FREE_GB", "3");
    proc_->setEnvironment(env);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(proc_, &ProcessRunner::finished, this, [this, conn](bool ok, int) {
        disconnect(*conn);
        if (!ok) { finish(false, QStringLiteral("The build could not be prepared: %1").arg(plainFailure())); return; }
        build();
    });
    QString err;
    if (!proc_->start(cmake, args, &err)) finish(false, QStringLiteral("CMake could not be started: %1").arg(err));
}

void BuildRunner::build() {
    HostTools host(appData_);
    const QString cmake = host.cmake().isEmpty() ? HostTools::which("cmake") : host.cmake();
    QStringList args = {"--build", buildDir(target_)};
    if (!spec_->buildTarget.isEmpty()) args << "--target" << spec_->buildTarget;
    emit progress(QStringLiteral("Compiling the game"), 0, 0, timer_.elapsed() / 1000, -1);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(proc_, &ProcessRunner::finished, this, [this, conn](bool ok, int) {
        disconnect(*conn);
        if (!ok) { finish(false, QStringLiteral("The build failed at step %1 of %2: %3").arg(lastDone_).arg(lastTotal_).arg(plainFailure())); return; }
        stage();
    });
    QString err;
    if (!proc_->start(cmake, args, &err)) finish(false, QStringLiteral("CMake could not be started: %1").arg(err));
}

void BuildRunner::stage() {
    if (spec_->distScript.isEmpty()) { finish(true, QString()); return; }
    const QString script = installer_->sourceDir() + "/" + spec_->distScript;
    emit progress(QStringLiteral("Packaging %1").arg(spec_->label), 0, 0, timer_.elapsed() / 1000, -1);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(proc_, &ProcessRunner::finished, this, [this, conn](bool ok, int) {
        disconnect(*conn);
        if (!ok) { finish(false, QStringLiteral("The packaging failed: %1").arg(plainFailure())); return; }
        finish(true, QString());
    });
    QString err;
#ifdef Q_OS_WIN
    const bool started = proc_->start("powershell", QStringList{"-ExecutionPolicy", "Bypass", "-File", script} + substitute(spec_->distArgs), &err);
#else
    const bool started = proc_->start("/bin/bash", QStringList{script} + substitute(spec_->distArgs), &err);
#endif
    if (!started) finish(false, QStringLiteral("The packaging script could not be started: %1").arg(err));
}

QString BuildRunner::plainFailure() const {
    // The first line that says what went wrong: a compiler's "error:", a Python exception, a "fatal". Ninja's own
    // "FAILED: <outputs>" line only names the step's output files, so it is the answer only when nothing says more.
    const QStringList lines = proc_->tail(120).split('\n');
    static const QRegularExpression said("error:|error [A-Z]+\\d+|fatal|Traceback|\\w+Error\\b|\\w+Exception\\b|not recognized|cannot|could not|"
                                         "No such file|Access is denied|out of memory|bad_alloc|killed",
                                         QRegularExpression::CaseInsensitiveOption);
    for (const QString& l : lines) {
        if (l.startsWith("FAILED:") || l.startsWith("ninja:")) continue;
        if (said.match(l).hasMatch()) return l.simplified().left(240);
    }
    // a Python script's last word is its exception, after the Traceback's frames
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines[i].startsWith("FAILED:") || lines[i].startsWith("ninja:") || lines[i].trimmed().isEmpty()) continue;
        if (!lines[i].startsWith("[")) return lines[i].simplified().left(240);
        break;
    }
    for (const QString& l : lines) {
        if (l.startsWith("FAILED:")) return l.simplified().left(240);
    }
    return lines.isEmpty() ? QStringLiteral("no output") : lines.last().simplified().left(240);
}

void BuildRunner::finish(bool ok, const QString& failure) {
    BuildRecord r;
    r.ok = ok;
    r.sourceCommit = installer_->sourceCommit();
    r.toolchainPins = installer_->toolchainPinsHash();
    r.builtAt = QDateTime::currentDateTime();
    r.seconds = timer_.elapsed() / 1000;
    r.failure = failure;
    const QString dir = buildDir(target_);
    if (!spec_->binary.isEmpty()) r.binaryPath = dir + "/" + spec_->binary;
    if (!spec_->distArtifact.isEmpty()) r.artifactPath = installer_->sourceDir() + "/" + spec_->distArtifact;
    else if (!spec_->artifactPath.isEmpty()) r.artifactPath = dir + "/" + spec_->artifactPath;
    else r.artifactPath = r.binaryPath;
    if (ok && !QFileInfo::exists(r.artifactPath)) { r.ok = false; r.failure = QStringLiteral("The build finished but produced no %1.").arg(QFileInfo(r.artifactPath).fileName()); }
    if (r.ok) r.artifactBytes = QFileInfo(r.artifactPath).size();
    saveRecord(target_, r);
    running_ = false;
    const QString t = target_;
    emit finished(t, r.ok, r);
}

QString BuildRunner::durationText(qint64 seconds) {
    if (seconds < 0) return QString();
    if (seconds < 60) return QStringLiteral("%1 s").arg(seconds);
    if (seconds < 3600) return QStringLiteral("%1 min %2 s").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

}  // namespace ps2r::launcher
