// SpanClassifier -- the four static passes (rules in the header).
#include "engine/vu/span_classifier.h"

void SpanClassifier::ComputeRunLenTouchHw(VuDecodedSlot* slots, size_t n) {
    for (size_t k = n; k-- > 0;) {
        VuDecodedSlot& s = slots[k];
        s.touch_hw = VuDecoder::VuSlotCanTouchHardwired(s) ? 1 : 0;
        const bool terminal_lower = !s.i_bit && (s.lower_kind == VUK_L_GENERIC || VuDecoder::IsRunTerminalLowerKind(s.lower_kind));
        const bool terminal = s.e_bit || s.is_nop_pair || terminal_lower || s.upper_kind == VUK_U_GENERIC;
        if (terminal || k + 1 >= n) {
            s.run_len = 1;
        } else {
            const unsigned next = slots[k + 1].run_len + 1u;
            s.run_len = static_cast<uint16_t>(next > 255u ? 255u : next);
        }
    }
}

void SpanClassifier::ComputeStallClasses(VuDecodedSlot* slots, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        VuDecodedSlot& s = slots[i];
        const bool lower = !s.i_bit;
        const bool rd = s.n_rd != 0;
        const bool ialu = s.ialu_wr_vi >= 0;
        uint8_t cls = 0;
        if (s.ialu_branch_rd != 0 && lower && !ialu && !s.efu_pipe_op && !s.fdiv_pipe_op && s.n_wr <= 1) {
            // A branch bundle (the lower reads VIs): classes 15-18 add the IALU branch stall.
            cls = static_cast<uint8_t>(15 + (rd ? 1 : 0) + (s.n_wr == 1 ? 2 : 0));
        } else if (s.ialu_branch_rd == 0 && !s.efu_pipe_op && !s.fdiv_pipe_op) {
            if (lower && !ialu) {
                if (rd) cls = (s.n_wr == 1) ? 1 : (s.n_wr == 2) ? 2 : 3;
                else cls = (s.n_wr == 1) ? 4 : (s.n_wr == 2) ? 5 : 6;
            } else if (!lower) {
                if (rd) cls = (s.n_wr == 1) ? 7 : (s.n_wr == 0) ? 8 : 0;
                else cls = (s.n_wr == 1) ? 9 : (s.n_wr == 0) ? 10 : 0;
            } else {  // lower && ialu
                if (rd) cls = (s.n_wr == 1) ? 11 : (s.n_wr == 0) ? 12 : 0;
                else cls = (s.n_wr == 1) ? 13 : (s.n_wr == 0) ? 14 : 0;
            }
        }
        s.stall_class = cls;
    }
}

void SpanClassifier::ComputeFoldClasses(VuDecodedSlot* slots, size_t n) {
    size_t head = 0;
    while (head < n) {
        size_t j = head;
        size_t p = 0;  // chain slots preceding j
        for (;;) {
            VuDecodedSlot& s = slots[j];
            if (s.run_len >= 2) {
                bool dq = false;
                if (s.run_len >= 250) {
                    dq = true;
                } else if (s.stall_class == 0) {
                    dq = true;
                } else if (s.efu_pipe_op || s.fdiv_pipe_op || s.ialu_wr_vi >= 0 || s.ialu_branch_rd != 0) {
                    dq = true;
                } else {
                    for (size_t back = 1; back <= 3 && back <= p && !dq; ++back) {
                        const VuDecodedSlot& q = slots[j - back];
                        for (int w = 0; w < q.n_wr && !dq; ++w) {
                            for (int r = 0; r < s.n_rd; ++r) {
                                if (q.wr_reg[w] == s.rd_reg[r] && (q.wr_mask[w] & s.rd_mask[r]) != 0) {
                                    dq = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                s.fold_class = dq ? 0 : (s.n_wr == 0 ? 15 : 16);
            }
            if (s.run_len <= 1) break;
            if (j + 1 >= n) break;
            ++j;
            ++p;
        }
        head = j + 1;
    }
}

void SpanClassifier::ComputeSpanRem(const VuDecodedSlot* slots, size_t n, std::vector<uint8_t>& span_rem) {
    span_rem.assign(n, 0);
    unsigned next = 0;
    for (size_t k = n; k-- > 0;) {
        const uint8_t fc = slots[k].fold_class;
        const unsigned v = (fc == 15 || fc == 16) ? (next == 255u ? 255u : next + 1u) : 0u;
        span_rem[k] = static_cast<uint8_t>(v);
        next = v;
    }
}
