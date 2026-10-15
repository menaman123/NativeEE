// GifsSection: the GIFS savestate section over GifRegs -- the 13-byte payload
// (ctrl, mode, stat = 0, path3_masked), round trip into a fresh register file
// byte-equal, restore clears the in-flight transfer and the FIFO count while
// CTRL / MODE / the VIF mask take the saved words (STAT recomposes from them),
// wrong lengths refused, REQUIRED through the registry, the restored callback.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/savestate/gifs_section.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

uint32_t Word(const std::vector<uint8_t>& v, size_t off) {
    uint32_t w = 0;
    std::memcpy(&w, v.data() + off, 4);
    return w;
}

}  // namespace

int main() {
    using namespace ps2r::savestate;
    std::printf("GifsSection\n");

    GifRegs src;
    src.Write(GifRegs::kBase + GifRegs::kCtrl, 4, 0x8u);   // PSE
    src.Write(GifRegs::kBase + GifRegs::kMode, 4, 0x5u);   // M3R | IMT
    src.SetPath3MaskedByVif(true);
    GifsSection section(src);
    std::vector<uint8_t> payload;
    section.Serialize(payload);
    Check(payload.size() == 13 && payload.size() == GifsSection::kPayloadBytes, "payload is 13 bytes");
    Check(Word(payload, 0) == 0x8u && Word(payload, 4) == 0x5u, "ctrl then mode");
    Check(Word(payload, 8) == 0, "stat word is the reference's never-written mirror (0)");
    Check(payload[12] == 1, "path3_masked byte");

    // Fresh register file with a transfer in flight.
    GifRegs dst;
    dst.Transfer().in_transfer = true;
    dst.Transfer().loop = 3;
    Check((dst.Read(GifRegs::kBase + GifRegs::kStat, 4) & (1u << 9)) != 0, "OPH set before restore");
    GifsSection restored(dst);
    int callbacks = 0;
    restored.SetOnRestored([&] { ++callbacks; });
    std::string err;
    Check(restored.Restore(payload, &err), ("restore ok: " + err).c_str());
    Check(dst.Ctrl() == 0x8u && dst.Mode() == 0x5u && dst.Path3MaskedByVif(), "CTRL / MODE / VIF mask restored");
    Check(!dst.Transfer().in_transfer && dst.Transfer().loop == 0, "in-flight transfer cleared");
    const uint32_t stat = static_cast<uint32_t>(dst.Read(GifRegs::kBase + GifRegs::kStat, 4));
    Check(stat == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)), "STAT recomposed: M3R M3P IMT PSE, no OPH, FQC 0");
    Check(callbacks == 1 && restored.RestoredCount() == 1, "restored callback ran once");
    std::vector<uint8_t> again;
    restored.Serialize(again);
    Check(again == payload, "re-serialize byte-identical");

    // The all-zero reference image round-trips to all zeros.
    GifRegs zero;
    GifsSection zs(zero);
    std::vector<uint8_t> zeros(13, 0);
    Check(zs.Restore(zeros, &err), "all-zero image restores");
    std::vector<uint8_t> zout;
    zs.Serialize(zout);
    Check(zout == zeros, "all-zero image re-serializes to 13 zero bytes");

    // Refusals.
    Check(!GifsSection::Validate(std::vector<uint8_t>(12), &err), "12 bytes refused");
    Check(!GifsSection::Validate(std::vector<uint8_t>(16), &err), "16 bytes refused");

    // Registry: REQUIRED, missing is a load error.
    SectionRegistry reg;
    section.Register(reg);
    const std::vector<Blob> cap = reg.Capture();
    Check(cap.size() == 1 && cap[0].tag == TAG_GIFS && (cap[0].flags & kSectionRequired) && cap[0].payload == payload,
          "registry capture: TAG_GIFS REQUIRED, same bytes");
    std::vector<Blob> none;
    Check(!reg.Validate(none, &err) && err.find("GIFS") != std::string::npos, ("missing GIFS is a load error: " + err).c_str());

    std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
