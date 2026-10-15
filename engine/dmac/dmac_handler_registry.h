#pragma once
// DmacHandlerRegistry: the guest DMAC interrupt handlers registered through
// AddDmacHandler (kernel syscall 18) as one flat table grouped by cause
// ascending, insertion order within a cause -- the dispatch order and the
// savestate record order. Mechanism only: the syscall table hands its records
// over at integration; the DMAC dispatches from it.
#include <cstddef>
#include <cstdint>
#include <vector>

// One registered handler; field-for-field the kernel's record.
struct DmacHandler {
    int id = 0;
    int cause = 0;
    uint32_t pc = 0;
    uint32_t gp = 0;
    uint32_t arg = 0;
    int flag = 0;
    bool active = true;
};

class DmacHandlerRegistry {
public:
    static constexpr int kMaxCause = 15;  // AddDmacHandler accepts causes 0..15
    static constexpr int kDispatchCauses = 10;  // RemoveDmacHandler / dispatch: the 10 channels

    // -1 when cause is outside 0..15 or pc is 0; otherwise the new id. `next`
    // 0 = head of the cause's chain, -1 = tail, else before the handler with
    // that id (tail when absent).
    int Add(int cause, uint32_t pc, uint32_t gp, uint32_t arg, int flag, int next);
    // -1 when cause is outside 0..9 or the id is not in that cause; otherwise
    // the number of handlers left on that cause.
    int Remove(int cause, int id);
    // Enable / disable by id across every cause; false when absent.
    bool SetActive(int id, bool active);

    // The handlers of one cause in dispatch order.
    std::vector<DmacHandler> ForCause(int cause) const;
    const std::vector<DmacHandler>& Table() const { return table_; }
    size_t Count() const { return table_.size(); }
    int NextId() const { return next_id_; }

    // Savestate / syscall-table hand-over: replace the table (re-grouped by
    // cause, stable) and the id counter.
    void Install(const std::vector<DmacHandler>& records, int next_id);
    void Clear();

private:
    size_t GroupEnd(int cause) const;
    std::vector<DmacHandler> table_;
    int next_id_ = 1;
};
