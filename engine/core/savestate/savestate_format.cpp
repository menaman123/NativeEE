// core/savestate/savestate_format.cpp -- .ps2st envelope codec (see the header).
#include "engine/core/savestate/savestate_format.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>

namespace ps2r::savestate {

namespace {

const uint32_t* CrcTable() {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    return table;
}

bool Fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

}  // namespace

void PutBytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
}

uint32_t Crc32(const void* data, size_t len, uint32_t seed) {
    const uint32_t* t = CrcTable();
    uint32_t c = seed ^ 0xFFFFFFFFu;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string TagName(uint32_t tag) {
    char tagc[5] = {(char)(tag & 0xFF), (char)((tag >> 8) & 0xFF), (char)((tag >> 16) & 0xFF),
                    (char)((tag >> 24) & 0xFF), 0};
    return std::string(tagc);
}

std::vector<uint8_t> SerializeStateFile(const FileMeta& meta, const std::vector<Blob>& sections) {
    size_t total = sizeof(FileHeader) + sizeof(uint32_t);
    for (const Blob& b : sections) total += sizeof(SectionHeader) + b.payload.size();
    std::vector<uint8_t> out;
    out.reserve(total);

    FileHeader hdr{};
    std::memcpy(hdr.magic, kMagic, sizeof(kMagic));
    hdr.version = meta.file_version;
    hdr.section_count = (uint32_t)sections.size();
    hdr.vsync_frame = meta.vsync_frame;
    hdr.flags = meta.flags;
    hdr.wall_ms = meta.wall_ms;
    PutPod(out, hdr);
    // The trailer CRC chains over the bytes in write order (finalized crc
    // re-opened per buffer -- Crc32's seed contract).
    uint32_t trailer = Crc32(&hdr, sizeof(hdr));

    for (const Blob& b : sections) {
        SectionHeader sh{};
        sh.tag = b.tag;
        sh.version = b.version;
        sh.flags = b.flags;
        sh.payload_len = b.payload.size();
        sh.crc32 = Crc32(b.payload.data(), b.payload.size());
        trailer = Crc32(&sh, sizeof(sh), trailer);
        PutPod(out, sh);
        if (!b.payload.empty()) {
            trailer = Crc32(b.payload.data(), b.payload.size(), trailer);
            PutBytes(out, b.payload.data(), b.payload.size());
        }
    }
    PutPod(out, trailer);
    return out;
}

bool WriteStateFile(const std::string& path, const FileMeta& meta, const std::vector<Blob>& sections,
                    std::string* err) {
    const std::vector<uint8_t> image = SerializeStateFile(meta, sections);
    const std::string tmp = path + ".tmp";
    {
        FilePtr f(std::fopen(tmp.c_str(), "wb"), &std::fclose);
        if (!f) {
            std::remove(tmp.c_str());
            return Fail(err, "cannot open for write: " + tmp);
        }
        if (std::fwrite(image.data(), 1, image.size(), f.get()) != image.size()) {
            std::remove(tmp.c_str());
            return Fail(err, "state write failed (tmp: " + tmp + ")");
        }
        if (std::fflush(f.get()) != 0) {
            std::remove(tmp.c_str());
            return Fail(err, "flush failed (tmp: " + tmp + ")");
        }
    }  // closed before the rename
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return Fail(err, "rename failed: " + tmp + " -> " + path + " (" + ec.message() + ")");
    }
    return true;
}

bool ParseStateFile(const uint8_t* data, size_t len, FileMeta* meta, std::vector<Blob>* sections,
                    std::string* err, const uint32_t* known_tags, size_t known_tag_count) {
    size_t off = 0;
    FileHeader hdr{};
    if (len < sizeof(hdr)) return Fail(err, "truncated: header");
    std::memcpy(&hdr, data, sizeof(hdr));
    off += sizeof(hdr);
    if (std::memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
        return Fail(err, "bad magic (not a PS2SAVE1 state file)");
    if (hdr.version < kMinFileVersion || hdr.version > kMaxFileVersion)
        return Fail(err, "unsupported version " + std::to_string(hdr.version) + " (runtime supports " +
                             std::to_string(kMinFileVersion) + ".." + std::to_string(kMaxFileVersion) + ")");

    uint32_t trailer = Crc32(&hdr, sizeof(hdr));
    std::vector<Blob> out;
    out.reserve(hdr.section_count);
    for (uint32_t i = 0; i < hdr.section_count; i++) {
        SectionHeader sh{};
        if (len - off < sizeof(sh)) return Fail(err, "truncated: section header " + std::to_string(i));
        std::memcpy(&sh, data + off, sizeof(sh));
        off += sizeof(sh);
        trailer = Crc32(&sh, sizeof(sh), trailer);
        if (sh.payload_len > kMaxSectionPayload) return Fail(err, "section payload too large");
        if (len - off < sh.payload_len) return Fail(err, "truncated: section payload " + std::to_string(i));

        Blob b;
        b.tag = sh.tag;
        b.version = sh.version;
        b.flags = sh.flags;
        b.payload.assign(data + off, data + off + (size_t)sh.payload_len);
        off += (size_t)sh.payload_len;
        if (Crc32(b.payload.data(), b.payload.size()) != sh.crc32)
            return Fail(err, "CRC mismatch in section '" + TagName(sh.tag) + "' (corrupt state file)");
        trailer = Crc32(b.payload.data(), b.payload.size(), trailer);

        if ((sh.flags & kSectionRequired) && known_tags) {
            bool known = false;
            for (size_t k = 0; k < known_tag_count; k++)
                if (known_tags[k] == sh.tag) { known = true; break; }
            if (!known)
                return Fail(err, "unknown REQUIRED section '" + TagName(sh.tag) + "' (state file from a newer build)");
        }
        out.push_back(std::move(b));
    }

    uint32_t file_trailer = 0;
    if (len - off < sizeof(file_trailer)) return Fail(err, "truncated: trailer");
    std::memcpy(&file_trailer, data + off, sizeof(file_trailer));
    if (file_trailer != trailer) return Fail(err, "trailer CRC mismatch (corrupt state file)");

    if (meta) {
        meta->vsync_frame = hdr.vsync_frame;
        meta->flags = hdr.flags;
        meta->wall_ms = hdr.wall_ms;
        meta->file_version = hdr.version;
    }
    if (sections) *sections = std::move(out);
    return true;
}

bool ReadStateFile(const std::string& path, FileMeta* meta, std::vector<Blob>* sections, std::string* err,
                   const uint32_t* known_tags, size_t known_tag_count) {
    FilePtr f(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!f) return Fail(err, "cannot open: " + path);
    std::vector<uint8_t> bytes;
    uint8_t buf[1 << 16];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f.get());
        if (n) bytes.insert(bytes.end(), buf, buf + n);
        if (n < sizeof(buf)) break;
    }
    return ParseStateFile(bytes.data(), bytes.size(), meta, sections, err, known_tags, known_tag_count);
}

}  // namespace ps2r::savestate
