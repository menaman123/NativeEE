// app/launcher/unavailable_prepare_runner.h -- the runner of a launcher that carries no prepare pipeline: available()
// is false with the reason, start() fails at once with the same plain sentence. The Preparing page then shows a real
// error state (the sentence, Retry, Details) instead of pretending.
#pragma once
#include "prepare_runner.h"
namespace ps2r::launcher {

class UnavailablePrepareRunner : public PrepareRunner {
    Q_OBJECT
public:
    explicit UnavailablePrepareRunner(QObject* parent = nullptr);
    bool available(const QString& targetId, QString* why) const override;
    QString estimateText(const QString& targetId) const override;
    bool start(const PrepareRequest& request, QString* error) override;
    void cancel() override {}
    bool running() const override { return false; }
    QString logPath() const override { return logPath_; }

private:
    QString logPath_;
};

}  // namespace ps2r::launcher
