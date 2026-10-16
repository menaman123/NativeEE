// IOP kernel result codes (PS2SDK iop/kernel/include/kerr.h).
#pragma once

namespace ps2r::iop {

struct IopError {
    static constexpr int Ok = 0;
    static constexpr int Error = -1;
    static constexpr int IllegalContext = -100;
    static constexpr int CpuDisabled = -102;
    static constexpr int NoMemory = -400;
    static constexpr int IllegalAttr = -401;
    static constexpr int IllegalEntry = -402;
    static constexpr int IllegalPriority = -403;
    static constexpr int IllegalStackSize = -404;
    static constexpr int IllegalThid = -406;
    static constexpr int UnknownThid = -407;
    static constexpr int UnknownSemid = -408;
    static constexpr int UnknownEvfid = -409;
    static constexpr int Dormant = -413;
    static constexpr int NotDormant = -414;
    static constexpr int CanNotWait = -417;
    static constexpr int SemaZero = -419;
    static constexpr int SemaOverflow = -420;
    static constexpr int EvfIllegalPattern = -421;
    static constexpr int EvfMulti = -422;
    static constexpr int EvfCondition = -423;
    static constexpr int WaitDelete = -425;
};

}  // namespace ps2r::iop
