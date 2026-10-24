// app/launcher/process_runner.cpp -- the child process with its log.
#include "process_runner.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
namespace ps2r::launcher {

ProcessRunner::ProcessRunner(QObject* parent) : QObject(parent), env_(QProcessEnvironment::systemEnvironment()) {}

ProcessRunner::~ProcessRunner() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(2000);
    }
}

void ProcessRunner::setLogPath(const QString& path) { logPath_ = path; }

bool ProcessRunner::start(const QString& program, const QStringList& args, QString* error) {
    if (process_) { process_->deleteLater(); process_ = nullptr; }
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setProcessEnvironment(env_);
    if (!workDir_.isEmpty()) process_->setWorkingDirectory(workDir_);
    connect(process_, &QProcess::readyReadStandardOutput, this, &ProcessRunner::onReadyRead);
    connect(process_, &QProcess::finished, this, &ProcessRunner::onFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            writeNote(QStringLiteral("could not start: %1").arg(process_->errorString()));
            finishedOk_ = false;
            exitCode_ = -1;
            emit finished(false, -1);
        }
    });
    if (!logPath_.isEmpty()) {
        QDir().mkpath(QFileInfo(logPath_).absolutePath());
        if (log_.isOpen()) log_.close();
        log_.setFileName(logPath_);
        if (!log_.open(QIODevice::WriteOnly | QIODevice::Text)) logPath_.clear();
    }
    commandLine_ = program + " " + args.join(' ');
    tail_.clear();
    partial_.clear();
    cancelled_ = false;
    finishedOk_ = false;
    exitCode_ = -1;
    writeNote(QStringLiteral("$ %1").arg(commandLine_));
    if (!workDir_.isEmpty()) writeNote(QStringLiteral("  in %1").arg(workDir_));
    timer_.start();
    process_->start(program, args);
    if (!process_->waitForStarted(10000)) {
        if (error) *error = process_->errorString();
        return false;
    }
    return true;
}

void ProcessRunner::cancel() {
    if (!process_ || process_->state() == QProcess::NotRunning) return;
    cancelled_ = true;
    writeNote(QStringLiteral("cancelled"));
    process_->terminate();
    if (!process_->waitForFinished(3000)) process_->kill();
}

bool ProcessRunner::running() const { return process_ && process_->state() != QProcess::NotRunning; }

qint64 ProcessRunner::processId() const { return process_ ? process_->processId() : 0; }

void ProcessRunner::writeNote(const QString& text) {
    if (log_.isOpen()) { log_.write((text + "\n").toUtf8()); log_.flush(); }
    emit line(text);
}

void ProcessRunner::onReadyRead() {
    handleText(QString::fromUtf8(process_->readAllStandardOutput()));
}

void ProcessRunner::handleText(const QString& text) {
    if (log_.isOpen()) { log_.write(text.toUtf8()); log_.flush(); }
    partial_ += text;
    // lines end with \n; a \r alone (a progress bar redrawing) also ends one
    int cut;
    while ((cut = partial_.indexOf(QRegularExpression("[\r\n]"))) >= 0) {
        const QString l = partial_.left(cut);
        partial_.remove(0, cut + 1);
        if (l.isEmpty()) continue;
        tail_ << l;
        while (tail_.size() > 200) tail_.removeFirst();
        int done = 0, total = 0;
        if (parseStepProgress(l, &done, &total)) emit progress(done, total);
        emit line(l);
    }
}

void ProcessRunner::onFinished(int code, QProcess::ExitStatus status) {
    if (!partial_.isEmpty()) { tail_ << partial_; emit line(partial_); partial_.clear(); }
    exitCode_ = status == QProcess::NormalExit ? code : -1;
    finishedOk_ = !cancelled_ && status == QProcess::NormalExit && code == 0;
    writeNote(QStringLiteral("exit %1 after %2 s%3").arg(exitCode_).arg(timer_.elapsed() / 1000).arg(cancelled_ ? " (cancelled)" : ""));
    if (log_.isOpen()) log_.close();
    emit finished(finishedOk_, exitCode_);
}

QString ProcessRunner::tail(int lines) const {
    const int n = qMin(lines, tail_.size());
    return QStringList(tail_.mid(tail_.size() - n)).join('\n');
}

bool ProcessRunner::parseStepProgress(const QString& line, int* done, int* total) {
    static const QRegularExpression re("^\\[(\\d+)/(\\d+)\\]");
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return false;
    *done = m.captured(1).toInt();
    *total = m.captured(2).toInt();
    return *total > 0;
}

}  // namespace ps2r::launcher
