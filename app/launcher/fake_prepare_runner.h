// app/launcher/fake_prepare_runner.h -- a runner that sleeps: progress ticks through named phases over a chosen
// number of seconds, then either writes an executable into the game folder and finishes, or fails at a chosen
// fraction with a plain sentence. Selected by PS2R_LAUNCHER_FAKE_PREPARE="ok:<seconds>" | "fail:<fraction>[:<seconds>]"
// | "hang" (never finishes; Cancel is the way out) so every Preparing screen is testable without the pipeline.
#pragma once
#include <QElapsedTimer>
#include <QStringList>
#include "prepare_runner.h"
class QTimer;
namespace ps2r::launcher {

class FakePrepareRunner : public PrepareRunner {
    Q_OBJECT
public:
    explicit FakePrepareRunner(const QString& spec, QObject* parent = nullptr);
    bool available(const QString& targetId, QString* why) const override;
    QString estimateText(const QString& targetId) const override;
    bool start(const PrepareRequest& request, QString* error) override;
    void cancel() override;
    bool running() const override { return running_; }
    QString logPath() const override { return request_.logPath; }
    static QStringList phases();

private:
    void tick();
    void writeLog(const QString& line);
    PrepareRequest request_;
    QTimer* timer_;
    QElapsedTimer clock_;
    double seconds_ = 3.0;
    double failAt_ = -1.0;   // < 0 = succeed
    bool hang_ = false, running_ = false;
    int lastPhase_ = -1;
};

}  // namespace ps2r::launcher
