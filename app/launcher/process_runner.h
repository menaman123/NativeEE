// app/launcher/process_runner.h -- one child process (the game, or whatever a prepare runner starts) with its output
// going to a log file under Logs/ and, line by line, to whoever listens (the Details sheet, the progress parser).
// `[n/m]` step lines are recognised and reported as progress.
#pragma once
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
namespace ps2r::launcher {

class ProcessRunner : public QObject {
    Q_OBJECT
public:
    explicit ProcessRunner(QObject* parent = nullptr);
    ~ProcessRunner() override;

    void setLogPath(const QString& path);
    QString logPath() const { return logPath_; }
    void setWorkingDirectory(const QString& dir) { workDir_ = dir; }
    void setEnvironment(const QProcessEnvironment& env) { env_ = env; }
    // Starts `program args`; false (with *error) when it cannot start at all.
    bool start(const QString& program, const QStringList& args, QString* error = nullptr);
    void cancel();
    bool running() const;
    qint64 processId() const;
    int exitCode() const { return exitCode_; }
    bool succeeded() const { return finishedOk_; }
    QString commandLine() const { return commandLine_; }
    QString tail(int lines = 30) const;   // the last lines seen (for a plain-English failure)
    qint64 elapsedMs() const { return timer_.elapsed(); }
    QProcess* process() { return process_; }
    void writeNote(const QString& text);  // a line written to the log and emitted as output (not from the child)

    // Parses a step line "[12/345] ..." into done/total; false for any other line.
    static bool parseStepProgress(const QString& line, int* done, int* total);

signals:
    void line(const QString& text);
    void progress(int done, int total);
    void finished(bool ok, int exitCode);

private:
    void onReadyRead();
    void onFinished(int code, QProcess::ExitStatus status);
    void handleText(const QString& text);
    QProcess* process_ = nullptr;
    QFile log_;
    QString logPath_, workDir_, commandLine_, partial_;
    QProcessEnvironment env_;
    QStringList tail_;
    QElapsedTimer timer_;
    int exitCode_ = -1;
    bool finishedOk_ = false, cancelled_ = false;
};

}  // namespace ps2r::launcher
