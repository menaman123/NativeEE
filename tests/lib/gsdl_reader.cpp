// tests/lib/gsdl_reader.cpp -- GsdlReader::Load.
#include "tests/lib/gsdl_reader.h"

#include <cstdio>
#include <cstring>

namespace ps2r {
namespace gs {

const char* GsdlReader::Recording::Name(uint32_t handler) const {
    for (const auto& n : names)
        if (n.first == handler) return n.second.c_str();
    return nullptr;
}

namespace {

// Appends one record to the recording; false = truncated (stop reading).
bool Consume(GsdlReader::Recording& r, GsdlReader::Frame& cur, const GsdlRecordHeader& h,
             const std::vector<uint8_t>& buf, size_t drawrec_sz) {
    if (h.kind == GsdlFormat::kDraw) {
        if (h.size < drawrec_sz) return false;
        GsdlReader::Draw d{};
        std::memcpy(&d.d, buf.data(), drawrec_sz);
        const size_t vbytes = static_cast<size_t>(d.d.vertex_count) * sizeof(VertexRec);
        if (d.d.verts_incl && h.size >= drawrec_sz + vbytes) {
            d.verts.resize(d.d.vertex_count);
            if (vbytes) std::memcpy(d.verts.data(), buf.data() + drawrec_sz, vbytes);
        }
        cur.draws.push_back(std::move(d));
        cur.order.push_back(1);
        return true;
    }
    if (h.kind == GsdlFormat::kName) {
        if (h.size < sizeof(GsdlNameRec)) return false;
        GsdlNameRec nr;
        std::memcpy(&nr, buf.data(), sizeof(nr));
        if (h.size >= sizeof(nr) + nr.name_len)
            r.names.emplace_back(nr.handler,
                                 std::string(reinterpret_cast<const char*>(buf.data()) + sizeof(nr),
                                             nr.name_len));
        return true;
    }
    if (h.kind == GsdlFormat::kJob || h.kind == GsdlFormat::kFrame) {
        if (h.size < sizeof(JobRec)) return false;
        GsdlReader::Job j;
        std::memcpy(&j.j, buf.data(), sizeof(JobRec));
        if (h.kind == GsdlFormat::kJob && j.j.payload_incl &&
            h.size >= sizeof(JobRec) + j.j.payload_bytes) {
            j.pixels.assign(buf.data() + sizeof(JobRec),
                            buf.data() + sizeof(JobRec) + j.j.payload_bytes);
        }
        if (h.kind == GsdlFormat::kFrame) {
            cur.vsync_frame = j.j.src_frame;
            cur.vsync = j.j;
            r.frames.push_back(std::move(cur));
            cur = GsdlReader::Frame();
        } else {
            cur.jobs.push_back(std::move(j));
            cur.order.push_back(0);
        }
        return true;
    }
    return true;  // unknown kinds are skipped
}

}  // namespace

bool GsdlReader::Load(const char* path, Recording& r) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    if (std::fread(&r.hdr, sizeof(r.hdr), 1, f) != 1 || r.hdr.magic != GsdlFormat::kMagic) {
        std::fprintf(stderr, "%s: not a drawlist recording\n", path);
        std::fclose(f);
        return false;
    }
    if (!GsdlFormat::ValidateHeader(r.hdr)) {
        std::fprintf(stderr, "%s: version %u, tool supports 1..%u\n", path, r.hdr.version,
                     GsdlFormat::kVersion);
        std::fclose(f);
        return false;
    }
    const size_t drawrec_sz = GsdlFormat::DrawRecSize(r.hdr.version);
    Frame cur;
    GsdlRecordHeader h;
    std::vector<uint8_t> buf;
    while (std::fread(&h, sizeof(h), 1, f) == 1) {
        buf.resize(h.size);
        if (h.size && std::fread(buf.data(), 1, h.size, f) != h.size) {
            r.truncated = true;
            break;
        }
        if (!Consume(r, cur, h, buf, drawrec_sz)) {
            r.truncated = true;
            break;
        }
    }
    if (!cur.draws.empty() || !cur.jobs.empty()) {
        cur.vsync_frame = UINT32_MAX;
        r.frames.push_back(std::move(cur));
    }
    std::fclose(f);
    return true;
}

}  // namespace gs
}  // namespace ps2r
