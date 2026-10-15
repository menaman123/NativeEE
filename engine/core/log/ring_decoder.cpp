// core/log/ring_decoder.cpp -- .rlog reader and the frozen text form.
#include "engine/core/log/ring_decoder.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "engine/core/log/ring_tags.h"

namespace ps2r {
namespace ringlog {

const char* RingDecoder::ChannelName(uint8_t ch) {
    switch (ch) {
        case CH_PERF:  return "PERF";
        case CH_EE:    return "EE";
        case CH_GFX:   return "GFX";
        case CH_INPUT: return "INPUT";
        case CH_MEM:   return "MEM";
        default:       return "?";
    }
}

const char* RingDecoder::ReasonName(uint32_t r) {
    switch (r) {
        case DUMP_CRASH:        return "crash";
        case DUMP_HOTKEY:       return "hotkey";
        case DUMP_EXIT:         return "exit";
        case DUMP_FATAL_NOJUMP: return "fatal_nojump";
        case DUMP_TEST:         return "test";
        default:                return "unknown";
    }
}

void RingDecoder::DecodePretty(const Event& e, char* out, size_t out_sz) {
    out[0] = 0;
    switch (e.tag) {
        case TAG_BOOT_SM_EDGE:
            std::snprintf(out, out_sz, "prev=%u cur=%u", (uint32_t)(e.a0 >> 32),
                          (uint32_t)e.a0);
            break;
        case TAG_MANUAL_DISPATCH:
            std::snprintf(out, out_sz, "pc=0x%" PRIx64 " a0reg=0x%x ra=0x%x", e.a0,
                          (uint32_t)(e.a1 >> 32), (uint32_t)e.a1);
            break;
        case TAG_INDIRECT_JUMP:
            std::snprintf(out, out_sz, "target=0x%" PRIx64 " src=0x%" PRIx64, e.a0,
                          e.a1);
            break;
        case TAG_FMV_PEND_EDGE:
            std::snprintf(out, out_sz, "prev=%u cur=%u pend %u->%u",
                          (uint32_t)((e.a0 >> 38) & 0x3f),
                          (uint32_t)((e.a0 >> 44) & 0x3f),
                          (uint32_t)((e.a0 >> 50) & 0x3f),
                          (uint32_t)((e.a1 >> 50) & 0x3f));
            break;
        case TAG_FMV_OPEN: {
            char nm[9];
            std::memcpy(nm, &e.a0, 8);
            nm[8] = 0;
            for (int i = 0; i < 8; i++)
                if (nm[i] && (nm[i] < 0x20 || nm[i] > 0x7e)) nm[i] = '.';
            std::snprintf(out, out_sz, "name=\"%s\" pre=0x%x post=0x%x ret=%u", nm,
                          (uint32_t)(e.a1 >> 48), (uint32_t)((e.a1 >> 32) & 0xffff),
                          (uint32_t)e.a1);
            break;
        }
        case TAG_FMV_ADV_ARGS:
            std::snprintf(out, out_sz, "idx=%u flag=%u ret=%u", (uint32_t)(e.a0 >> 32),
                          (uint32_t)e.a0, (uint32_t)e.a1);
            break;
        case TAG_TSIG:
            std::snprintf(out, out_sz, "fbp=0x%" PRIx64 " batches=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_DISPFB_FLIP:
            std::snprintf(out, out_sz, "fb=0x%" PRIx64 " seq=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_FRAME_PRESENT:
            std::snprintf(out, out_sz, "draws_cum=%" PRIu64 " qdepth=%" PRIu64, e.a0,
                          e.a1);
            break;
        case TAG_INLAT_T1:
        case TAG_INLAT_T2:
        case TAG_INLAT_T3:
            std::snprintf(out, out_sz, "t_us=%" PRIu64 " old=0x%04x new=0x%04x syncv=%u",
                          e.a0, (uint32_t)((e.a1 >> 16) & 0xffff), (uint32_t)(e.a1 & 0xffff),
                          (uint32_t)(e.a1 >> 32));
            break;
        case TAG_INLAT_T5:
            std::snprintf(out, out_sz, "t_us=%" PRIu64 " ee_frame=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_INLAT_T4G:
            std::snprintf(out, out_sz, "t_us=%" PRIu64 " ee_frame=%u old=0x%04x new=0x%04x",
                          e.a0, (uint32_t)(e.a1 >> 32), (uint32_t)((e.a1 >> 16) & 0xffff),
                          (uint32_t)(e.a1 & 0xffff));
            break;
        case TAG_INLAT_RATE:
            std::snprintf(out, out_sz, "site=%" PRIu64 " calls_per_s=%u syncv=%u", e.a0,
                          (uint32_t)e.a1, (uint32_t)(e.a1 >> 32));
            break;
        case TAG_FRAME_TIME:
            std::snprintf(out, out_sz, "t_us=%" PRIu64 " interval_us=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_REGIME_CENSUS:
            std::snprintf(out, out_sz, "inj=%u flips=%u ee=%u tick=%u", (uint32_t)(e.a0 >> 32),
                          (uint32_t)e.a0, (uint32_t)(e.a1 >> 32), (uint32_t)e.a1);
            break;
        case TAG_MTVU_SYNC_SITE:
            std::snprintf(out, out_sz, "site=%" PRIu64 " hits=%u blocked=%u", e.a0,
                          (uint32_t)(e.a1 >> 32), (uint32_t)e.a1);
            break;
        case TAG_DEFER_CENSUS:
            std::snprintf(out, out_sz, "enq=%u del=%u fence=%u wbusy_ms=%.1f",
                          (uint32_t)(e.a0 >> 32), (uint32_t)e.a0, (uint32_t)(e.a1 >> 32),
                          (double)((uint32_t)e.a1) / 10.0);
            break;
        case TAG_MEM_GAUGE:
            std::snprintf(out, out_sz, "gauge=%" PRIu64 " bytes=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_MEM_GAUGE_COUNT:
            std::snprintf(out, out_sz, "gauge=%" PRIu64 " count=%" PRIu64, e.a0, e.a1);
            break;
        case TAG_RSS_SAMPLE:
            std::snprintf(out, out_sz, "footprint_mb=%.1f resident_mb=%.1f",
                          (double)e.a0 / (1024.0 * 1024.0), (double)e.a1 / (1024.0 * 1024.0));
            break;
        case TAG_MTVU_BACKPRESSURE:
            std::snprintf(out, out_sz, "pipe_mb=%.1f wait_ms=%.2f",
                          (double)e.a0 / (1024.0 * 1024.0), (double)e.a1 / 1e6);
            break;
        case TAG_RUNAHEAD_DEPTH:
            std::snprintf(out, out_sz,
                          "ee_pushed=%" PRIu64 " processed=%" PRIu64 " depth_frames=%lld", e.a0,
                          e.a1, (long long)((int64_t)e.a0 - (int64_t)e.a1));
            break;
        case TAG_RUNAHEAD_ALARM:
            std::snprintf(out, out_sz, "peak_depth=%" PRIu64 " cap=%u overrun_presents=%u", e.a0,
                          (uint32_t)(e.a1 >> 32), (uint32_t)e.a1);
            break;
        case TAG_SLOWSPOT:
            std::snprintf(out, out_sz, "p50_ms=%.1f p95_ms=%.1f threshold_ms=%.1f window=%u",
                          (double)(uint32_t)(e.a0 >> 32) / 1000.0, (double)(uint32_t)e.a0 / 1000.0,
                          (double)(uint32_t)(e.a1 >> 32) / 1000.0, (uint32_t)e.a1);
            break;
        default:
            break;
    }
}

bool RingDecoder::ReadRlog(const char* path, DecodedFile& out, std::string* err,
                           bool allow_partial) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        if (err) *err = "cannot open";
        return false;
    }
    bool ok = true;
    do {
        if (std::fread(&out.hdr, sizeof(out.hdr), 1, f) != 1) {
            if (err) *err = "short header";
            ok = false;
            break;
        }
        if (std::memcmp(out.hdr.magic, kMagic, 8) != 0) {
            if (err) *err = "bad magic";
            ok = false;
            break;
        }
        if (out.hdr.version != kVersion || out.hdr.event_size != sizeof(Event)) {
            if (err) *err = "version/event_size mismatch";
            ok = false;
            break;
        }
        for (uint32_t r = 0; r < out.hdr.ring_count; r++) {
            RingHeader rh{};
            if (std::fread(&rh, sizeof(rh), 1, f) != 1) {
                if (err) *err = "short ring header";
                ok = allow_partial;
                break;
            }
            if (rh.name_len > kNameMax || rh.retained > (1u << 24)) {
                if (err) *err = "implausible ring header";
                ok = false;
                break;
            }
            DecodedRing dr;
            dr.name.resize(rh.name_len);
            if (rh.name_len && std::fread(&dr.name[0], 1, rh.name_len, f) != rh.name_len) {
                if (err) *err = "short ring name";
                ok = allow_partial;
                break;
            }
            dr.total_emitted = rh.total_emitted;
            dr.events.resize(rh.retained);
            if (rh.retained &&
                std::fread(dr.events.data(), sizeof(Event), rh.retained, f) != rh.retained) {
                if (err) *err = "short event section";
                dr.events.clear();
                ok = allow_partial;
                out.rings.push_back(std::move(dr));
                break;
            }
            out.rings.push_back(std::move(dr));
        }
    } while (false);
    std::fclose(f);
    return ok;
}

uint64_t RingDecoder::WriteDecodedText(const DecodedFile& df, const char* src_path,
                                       std::FILE* out) {
    uint64_t drops = 0;
    for (const auto& r : df.rings) drops += r.total_emitted - (uint64_t)r.events.size();
    std::fprintf(out,
                 "RLHDR file=%s reason=%s frame=%u rings=%u drops=%" PRIu64 " wall_ms=%" PRIu64
                 " slots_exhausted=%u\n",
                 src_path, ReasonName(df.hdr.reason), df.hdr.vsync_frame,
                 (uint32_t)df.rings.size(), drops, df.hdr.wall_ms, df.hdr.slots_exhausted);

    struct Row {
        uint32_t     frame;
        uint32_t     ring;
        uint64_t     seq;
        const Event* e;
    };
    std::vector<Row> rows;
    for (uint32_t ri = 0; ri < df.rings.size(); ri++) {
        const auto& r = df.rings[ri];
        std::fprintf(out, "RLRING ring=%s emitted=%" PRIu64 " retained=%zu dropped=%" PRIu64 "\n",
                     r.name.c_str(), r.total_emitted, r.events.size(),
                     r.total_emitted - (uint64_t)r.events.size());
        const uint64_t start_seq = r.total_emitted - (uint64_t)r.events.size();
        rows.reserve(rows.size() + r.events.size());
        for (size_t i = 0; i < r.events.size(); i++)
            rows.push_back(Row{r.events[i].frame, ri, start_seq + i, &r.events[i]});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        if (a.ring != b.ring) return a.ring < b.ring;
        return a.seq < b.seq;
    });

    uint64_t n = 0;
    char pretty[192];
    for (const Row& row : rows) {
        const Event& e = *row.e;
        const TagInfo* ti = FindTag(e.tag);
        DecodePretty(e, pretty, sizeof(pretty));
        std::fprintf(out,
                     "RL f=%u %s %s %s seq=%" PRIu64 " a0=0x%" PRIx64 " a1=0x%" PRIx64 "%s%s%s\n",
                     e.frame, df.rings[row.ring].name.c_str(), ChannelName(e.channel),
                     ti ? ti->name : "UNKNOWN_TAG", row.seq, e.a0, e.a1,
                     (e.flags & EVF_VERBOSE) ? " V" : "", pretty[0] ? " " : "", pretty);
        n++;
    }
    return n;
}

bool RingDecoder::DecodeFileToText(const char* rlog_path, const char* txt_path) {
    DecodedFile df;
    std::string err;
    if (!ReadRlog(rlog_path, df, &err)) {
        std::fprintf(stderr, "ringlog: decode %s failed: %s\n", rlog_path, err.c_str());
        return false;
    }
    std::FILE* out = std::fopen(txt_path, "w");
    if (!out) return false;
    WriteDecodedText(df, rlog_path, out);
    std::fclose(out);
    return true;
}

}  // namespace ringlog
}  // namespace ps2r
