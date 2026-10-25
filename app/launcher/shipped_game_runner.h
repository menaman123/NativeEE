// app/launcher/shipped_game_runner.h -- the runner of a release that carries the game ready-made: a `game/` folder
// beside the launcher's folder (the zip's <top>/game/ next to <top>/launcher/; on macOS beside the .app) holding this
// computer's game executable and everything it reads beside itself. Preparing = copying that folder into the game
// folder on a worker thread (progress by bytes), so the game folder stays the one portable place the game lives. The
// disc was already checked by its content when it was added; nothing is fetched and nothing is made on this machine.
// createDefault() picks this runner whenever such a folder is present.
#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include "prepare_runner.h"
namespace ps2r::launcher {

class ShippedGameRunner : public PrepareRunner {
    Q_OBJECT
public:
    // `shippedDir` holds the game; `hostTarget` is the manifest target it was made for (this computer's).
    ShippedGameRunner(const QString& shippedDir, const QString& hostTarget, const QString& configFileName, QObject* parent = nullptr);
    ~ShippedGameRunner() override;
    bool available(const QString& targetId, QString* why) const override;
    QString estimateText(const QString& targetId) const override;
    bool start(const PrepareRequest& request, QString* error) override;
    void cancel() override;
    bool running() const override { return running_->load(); }
    QString logPath() const override { return request_.logPath; }
    // The shipped game folder beside this launcher, when it holds `binary` (the host target's executable); else empty.
    static QString findShipped(const QString& binary);
    QString shippedDir() const { return shippedDir_; }

private:
    QString shippedDir_, hostTarget_, configFileName_;
    PrepareRequest request_;
    std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
    std::thread worker_;
};

}  // namespace ps2r::launcher
