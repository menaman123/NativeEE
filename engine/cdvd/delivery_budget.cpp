// DeliveryBudget implementation; the rules are in the header.
#include "engine/cdvd/delivery_budget.h"

DeliveryBudget::Result DeliveryBudget::Charge(uint64_t now_epoch, uint64_t bytes) {
    const auto result = ReadTiming::Admit(*state_, now_epoch, bytes);
    return {result.allowed, result.refilled};
}
