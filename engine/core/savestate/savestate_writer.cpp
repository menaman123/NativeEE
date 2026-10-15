// core/savestate/savestate_writer.cpp
#include "engine/core/savestate/savestate_writer.h"

#include <chrono>
#include <utility>

namespace ps2r::savestate {

SavestateWriter::~SavestateWriter() { JoinForExit(); }

bool SavestateWriter::TryEnqueue(WriteJob&& job) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (pending_) return false;
        if (!started_) {
            started_ = true;
            stop_ = false;
            thread_ = std::thread(&SavestateWriter::WriterMain, this);
        }
        job_ = std::move(job);
        pending_ = true;
    }
    wake_.Notify();
    return true;
}

bool SavestateWriter::Busy() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_;
}

void SavestateWriter::Flush() {
    for (;;) {
        const uint64_t gen = idle_.Generation();
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!started_ || !pending_) return;
        }
        idle_.Wait(gen);
    }
}

void SavestateWriter::JoinForExit() {
    Flush();
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!started_) return;
        stop_ = true;
        started_ = false;
        to_join = std::move(thread_);
    }
    wake_.Notify();
    if (to_join.joinable()) to_join.join();
}

WriteResult SavestateWriter::LastResult() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_;
}

uint64_t SavestateWriter::CompletedCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return completed_;
}

void SavestateWriter::WriterMain() {
    for (;;) {
        const uint64_t gen = wake_.Generation();
        WriteJob job;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (pending_) {
                job = std::move(job_);
                have = true;
            } else if (stop_) {
                return;
            }
        }
        if (!have) {
            wake_.Wait(gen);
            continue;
        }
        WriteResult r;
        r.path = job.path;
        const auto t0 = std::chrono::steady_clock::now();
        r.ok = WriteStateFile(job.path, job.meta, job.sections, &r.error);
        r.write_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0).count();
        r.bytes = sizeof(FileHeader) + sizeof(uint32_t);
        for (const Blob& b : job.sections) r.bytes += sizeof(SectionHeader) + b.payload.size();
        {
            std::lock_guard<std::mutex> lk(mu_);
            last_ = std::move(r);
            completed_++;
            pending_ = false;
        }
        idle_.Notify();
    }
}

}  // namespace ps2r::savestate
