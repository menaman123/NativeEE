// engine/iop/hle/sysclib/sysclib_module.cpp -- SysclibModule bodies.
#include "engine/iop/hle/sysclib/sysclib_module.h"

#include <cstdlib>
#include <cstring>

namespace ps2r::iop::hle {

namespace {
const SysclibModule::OrdinalEntry kOrdinals[SysclibModule::kOrdinalCount] = {
    {7, "tolower", &SysclibModule::tolower},  {12, "memcpy", &SysclibModule::memcpy},
    {13, "memmove", &SysclibModule::memmove}, {14, "memset", &SysclibModule::memset},
    {16, "bcopy", &SysclibModule::bcopy},     {17, "bzero", &SysclibModule::bzero},
    {20, "strcat", &SysclibModule::strcat},   {22, "strcmp", &SysclibModule::strcmp},
    {23, "strcpy", &SysclibModule::strcpy},   {27, "strlen", &SysclibModule::strlen},
    {29, "strncmp", &SysclibModule::strncmp}, {34, "strstr", &SysclibModule::strstr},
    {36, "strtol", &SysclibModule::strtol},   {40, "wmemcopy", &SysclibModule::wmemcopy},
};
}  // namespace

const SysclibModule::OrdinalEntry* SysclibModule::OrdinalTable() { return kOrdinals; }

const SysclibModule::OrdinalEntry* SysclibModule::FindOrdinal(uint32_t ordinal) {
    for (const auto& e : kOrdinals)
        if (e.ordinal == ordinal) return &e;
    return nullptr;
}

size_t SysclibModule::CStringLength(uint32_t addr) const {
    const uint32_t start = addr & IopMemory::kMask;
    const uint8_t* p = mem_.Pointer(start, 1);
    if (!p) return 0;
    const size_t avail = IopMemory::kSize - start;
    const void* nul = std::memchr(p, 0, avail);
    return nul ? static_cast<size_t>(static_cast<const uint8_t*>(nul) - p) : avail;
}

void SysclibModule::memcpy(IopContext& ctx) {
    const uint32_t n = ctx.gpr[6];
    uint8_t* dst = mem_.Pointer(ctx.gpr[4], n);
    const uint8_t* src = mem_.Pointer(ctx.gpr[5], n);
    if (dst && src && n) std::memmove(dst, src, n);
    ctx.gpr[2] = ctx.gpr[4];
}

void SysclibModule::tolower(IopContext& ctx) {
    const uint32_t c = ctx.gpr[4];
    ctx.gpr[2] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

void SysclibModule::memmove(IopContext& ctx) { memcpy(ctx); }

void SysclibModule::bcopy(IopContext& ctx) {
    const uint32_t n = ctx.gpr[6];
    const uint8_t* src = mem_.Pointer(ctx.gpr[4], n);
    uint8_t* dst = mem_.Pointer(ctx.gpr[5], n);
    if (dst && src && n) std::memmove(dst, src, n);
    ctx.gpr[2] = 0;
}

void SysclibModule::bzero(IopContext& ctx) {
    const uint32_t n = ctx.gpr[5];
    uint8_t* dst = mem_.Pointer(ctx.gpr[4], n);
    if (dst && n) std::memset(dst, 0, n);
    ctx.gpr[2] = 0;
}

void SysclibModule::strcmp(IopContext& ctx) {
    const size_t la = CStringLength(ctx.gpr[4]), lb = CStringLength(ctx.gpr[5]);
    const uint8_t* a = mem_.Pointer(ctx.gpr[4], static_cast<uint32_t>(la) + 1);
    const uint8_t* b = mem_.Pointer(ctx.gpr[5], static_cast<uint32_t>(lb) + 1);
    if (!a || !b) {
        ctx.gpr[2] = 0;
        return;
    }
    const int r = std::strcmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
    ctx.gpr[2] = static_cast<uint32_t>(r < 0 ? -1 : (r > 0 ? 1 : 0));
}

void SysclibModule::strtol(IopContext& ctx) {
    const size_t len = CStringLength(ctx.gpr[4]);
    const uint8_t* s = mem_.Pointer(ctx.gpr[4], static_cast<uint32_t>(len) + 1);
    if (!s) {
        ctx.gpr[2] = 0;
        return;
    }
    const char* begin = reinterpret_cast<const char*>(s);
    char* end = nullptr;
    const long v = std::strtol(begin, &end, static_cast<int>(static_cast<int32_t>(ctx.gpr[6])));
    if (ctx.gpr[5]) mem_.Write<uint32_t>(ctx.gpr[5], ctx.gpr[4] + static_cast<uint32_t>(end - begin));
    ctx.gpr[2] = static_cast<uint32_t>(static_cast<int32_t>(v));
}

void SysclibModule::wmemcopy(IopContext& ctx) {
    const uint32_t n = ctx.gpr[6] & ~3u;
    uint8_t* dst = mem_.Pointer(ctx.gpr[4], n);
    const uint8_t* src = mem_.Pointer(ctx.gpr[5], n);
    if (dst && src && n) std::memmove(dst, src, n);
    ctx.gpr[2] = ctx.gpr[4];
}

void SysclibModule::memset(IopContext& ctx) {
    const uint32_t n = ctx.gpr[6];
    uint8_t* dst = mem_.Pointer(ctx.gpr[4], n);
    if (dst && n) std::memset(dst, static_cast<int>(ctx.gpr[5] & 0xFF), n);
    ctx.gpr[2] = ctx.gpr[4];
}

void SysclibModule::strcat(IopContext& ctx) {
    const size_t dlen = CStringLength(ctx.gpr[4]);
    const size_t slen = CStringLength(ctx.gpr[5]);
    uint8_t* dst = mem_.Pointer(ctx.gpr[4] + static_cast<uint32_t>(dlen), static_cast<uint32_t>(slen) + 1);
    const uint8_t* src = mem_.Pointer(ctx.gpr[5], static_cast<uint32_t>(slen) + 1);
    if (dst && src) std::memmove(dst, src, slen + 1);
    ctx.gpr[2] = ctx.gpr[4];
}

void SysclibModule::strcpy(IopContext& ctx) {
    const size_t slen = CStringLength(ctx.gpr[5]);
    uint8_t* dst = mem_.Pointer(ctx.gpr[4], static_cast<uint32_t>(slen) + 1);
    const uint8_t* src = mem_.Pointer(ctx.gpr[5], static_cast<uint32_t>(slen) + 1);
    if (dst && src) std::memmove(dst, src, slen + 1);
    ctx.gpr[2] = ctx.gpr[4];
}

void SysclibModule::strlen(IopContext& ctx) {
    ctx.gpr[2] = static_cast<uint32_t>(CStringLength(ctx.gpr[4]));
}

size_t SysclibModule::CompareLength(uint32_t addr, uint32_t n) const {
    // strncmp looks at min(n, strlen + 1) bytes (the terminator takes part in the
    // comparison). A string that runs unterminated to the end of RAM has no
    // terminator byte to read: never look past the last byte of RAM.
    const size_t avail = IopMemory::kSize - (addr & IopMemory::kMask);
    size_t lim = CStringLength(addr) + 1;
    if (lim > avail) lim = avail;
    return n < lim ? n : lim;
}

void SysclibModule::strncmp(IopContext& ctx) {
    const uint32_t n = ctx.gpr[6];
    const uint8_t* a = mem_.Pointer(ctx.gpr[4], 1);
    const uint8_t* b = mem_.Pointer(ctx.gpr[5], 1);
    int r = 0;
    if (a && b) {
        // Compare like strncmp over the bounded strings; both limits stay inside RAM,
        // so the tail byte read below (index m < max(lim, limb)) is always in bounds.
        const size_t lim = CompareLength(ctx.gpr[4], n);
        const size_t limb = CompareLength(ctx.gpr[5], n);
        const size_t m = lim < limb ? lim : limb;
        r = std::memcmp(a, b, m);
        if (r == 0 && lim != limb) r = lim < limb ? -static_cast<int>(b[m]) : static_cast<int>(a[m]);
    }
    ctx.gpr[2] = static_cast<uint32_t>(r);
}

void SysclibModule::strstr(IopContext& ctx) {
    const size_t lh = CStringLength(ctx.gpr[4]), ln = CStringLength(ctx.gpr[5]);
    const uint8_t* hay = mem_.Pointer(ctx.gpr[4], 1);
    const uint8_t* needle = mem_.Pointer(ctx.gpr[5], 1);
    uint32_t r = 0;
    if (hay && needle) {
        if (ln == 0) {
            r = ctx.gpr[4];
        } else if (ln <= lh) {
            for (size_t i = 0; i + ln <= lh; ++i) {
                if (std::memcmp(hay + i, needle, ln) == 0) { r = ctx.gpr[4] + static_cast<uint32_t>(i); break; }
            }
        }
    }
    ctx.gpr[2] = r;
}

}  // namespace ps2r::iop::hle
