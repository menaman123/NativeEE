// core/log/ring_tags.cpp -- the tag registry table instance.
#include "engine/core/log/ring_tags.h"

namespace ps2r {
namespace ringlog {

const TagInfo* TagTable(size_t* count) {
    static const TagInfo table[] = {
#define PS2R_RING_TAG_ROW(sym, id, ch, d0, d1) {id, ch, #sym, d0, d1},
        PS2R_RING_TAG_TABLE(PS2R_RING_TAG_ROW)
#undef PS2R_RING_TAG_ROW
    };
    if (count) *count = sizeof(table) / sizeof(table[0]);
    return table;
}

const TagInfo* FindTag(uint16_t id) {
    size_t n = 0;
    const TagInfo* t = TagTable(&n);
    for (size_t i = 0; i < n; i++)
        if (t[i].id == id) return &t[i];
    return nullptr;
}

}  // namespace ringlog
}  // namespace ps2r
