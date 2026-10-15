#pragma once
// SifState: the EE-side SIF register block 0x1000F200..0x1000F2FF registered
// on the MmioRouter (displacing the Phase-3 idle sink for that page).
//
// PS2 rule (EE User's Manual, SIF registers): MSCOM (+0x00) and SMCOM (+0x10)
// are the main->sub / sub->main command-buffer words; MSFLAG (+0x20) and
// SMFLAG (+0x30) are the flag words the two CPUs handshake through
// (SIF_STAT_SIFINIT 0x10000, CMDINIT 0x20000, BOOTEND 0x40000); +0x40 is the
// control word. The original runtime stored every write as a plain word and
// read the stored word back (memory.cpp:962-971 reads, :1108-1114 writes);
// offsets outside the five words read 0 and drop writes. This class keeps the
// same observable behaviour; the IOP side (sifman / the boot handshake) sets
// SMFLAG bits through SetSmflagBits.
#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"

class MmioRouter;

class SifState : public MmioHandler {
public:
    static constexpr uint32_t kBase = 0x1000F200u;
    static constexpr uint32_t kSize = 0x100u;
    enum Offset : uint32_t { kMscom = 0x00, kSmcom = 0x10, kMsflag = 0x20, kSmflag = 0x30, kCtrl = 0x40 };
    enum Flag : uint32_t { kSifInit = 0x10000u, kCmdInit = 0x20000u, kBootEnd = 0x40000u };

    void Register(MmioRouter& router);

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    uint32_t Mscom() const { return mscom_; }
    uint32_t Smcom() const { return smcom_; }
    uint32_t Msflag() const { return msflag_; }
    uint32_t Smflag() const { return smflag_; }
    uint32_t Ctrl() const { return ctrl_; }

    // The IOP-side face: the sub CPU raises SMFLAG bits (CMDINIT after
    // sceSifInitCmd, BOOTEND after the reboot) and clears MSFLAG bits it
    // has consumed.
    void SetSmflagBits(uint32_t bits) { smflag_ |= bits; }
    void ClearMsflagBits(uint32_t bits) { msflag_ &= ~bits; }
    void Reset();

    uint64_t ReadCount() const { return reads_; }
    uint64_t WriteCount() const { return writes_; }

private:
    uint32_t* Word(uint32_t offset);
    uint32_t WordValue(uint32_t offset) const;

    uint32_t mscom_ = 0;
    uint32_t smcom_ = 0;
    uint32_t msflag_ = 0;
    uint32_t smflag_ = 0;
    uint32_t ctrl_ = 0;
    uint64_t reads_ = 0;
    uint64_t writes_ = 0;
};
