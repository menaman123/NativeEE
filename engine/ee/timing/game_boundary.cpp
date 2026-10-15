#include "engine/ee/timing/game_boundary.h"

#include <utility>

#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/timing/boundary_sequencer.h"

namespace {
struct CompletionScope {
    const GameBoundary* owner;
    CpuContext* context;
    CompletionScope* previous;
};
thread_local CompletionScope* completion_scope = nullptr;
}

GameBoundary::GameBoundary(ThreadManager& threads, BoundarySequencer& sequence)
    : threads_(threads), sequence_(sequence),
      capture_(threads, {DispatchTable::CanResumeAt, ShadowCallStack::Complete, DispatchTable::CanRestartWait}) {
    if (capture_.Attached())
        attached_ = sequence_.AttachCompletedService(this,
            [this](CpuContext& ctx, uint64_t frame) { Completed(ctx, frame); },
            DispatchTable::AfterCurrentReturn);
}

GameBoundary::~GameBoundary() { sequence_.DetachCompletedService(this); }
bool GameBoundary::Attached() const { return attached_ && capture_.Attached(); }
void GameBoundary::SetConsumer(Consumer consumer) { consumer_ = std::move(consumer); }

void GameBoundary::Completed(CpuContext& ctx, uint64_t frame) {
    CompletionScope scope{this, &ctx, completion_scope};
    completion_scope = &scope;
    try {
        if (consumer_) consumer_(frame);
    } catch (...) {
        completion_scope = scope.previous;
        throw;
    }
    completion_scope = scope.previous;
}

bool GameBoundary::FreshThreads(ThreadCapture::Records& out, std::string& error) {
    out.clear();
    if (!Attached() || !completion_scope || completion_scope->owner != this || completion_scope->previous) {
        error = "Fresh capture requires this owner's completed game-boundary callback.";
        return false;
    }
    return capture_.CaptureBoundary(threads_.CurrentTid(), *completion_scope->context, out, error);
}

void GameBoundary::ResetAfterRestore() {
    capture_.ResetAfterRestore();
    sequence_.ResetServiceCompletion();
}
