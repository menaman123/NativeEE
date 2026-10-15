#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class DeliveryMeter;
class LoaderModel;

struct StreamPersistenceActions {
    std::function<void(uint32_t, uint32_t, uint32_t, bool)> window;
    std::function<void(uint32_t)> wait;
    std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)> raw;
};

int RunStreamPersistenceCase(uint64_t& tick, DeliveryMeter& meter,
    LoaderModel& model, const std::vector<uint32_t>& calls, int32_t reentrant_file,
    const StreamPersistenceActions& actions);
