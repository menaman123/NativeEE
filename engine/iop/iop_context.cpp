// engine/iop/iop_context.cpp -- IopContext entry-state construction.
#include "engine/iop/iop_context.h"

namespace ps2r::iop {

IopContext IopContext::ForEntry(uint32_t sp, uint32_t gp) {
    IopContext c;
    c.gpr[29] = sp;
    c.gpr[28] = gp;
    return c;
}

}  // namespace ps2r::iop
