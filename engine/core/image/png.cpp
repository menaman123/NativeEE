// core/image/png.cpp -- Png.
#include "engine/core/image/png.h"
#include "engine/core/image/png_bit_reader.h"
#include "engine/core/image/png_huffman.h"

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ps2r {
namespace image {
namespace {

uint32_t Be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v));
}
bool Fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

constexpr uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                      3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool InflateCodes(PngBitReader& in, const PngHuffman& lit, const PngHuffman& dist, std::vector<uint8_t>& out,
                  std::string* error) {
    for (;;) {
        int symbol = 0;
        if (!lit.Decode(in, symbol)) return Fail(error, "deflate: truncated literal/length code");
        if (symbol < 256) { out.push_back(uint8_t(symbol)); continue; }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) return Fail(error, "deflate: invalid length code");
        uint32_t extra = 0;
        if (!in.Bits(kLengthExtra[symbol], extra)) return Fail(error, "deflate: truncated length");
        const size_t length = kLengthBase[symbol] + extra;
        int d = 0;
        if (!dist.Decode(in, d) || d >= 30) return Fail(error, "deflate: invalid distance code");
        if (!in.Bits(kDistExtra[d], extra)) return Fail(error, "deflate: truncated distance");
        const size_t distance = kDistBase[d] + extra;
        if (distance > out.size()) return Fail(error, "deflate: distance before the start of output");
        const size_t from = out.size() - distance;
        for (size_t i = 0; i < length; ++i) out.push_back(out[from + i]);   // overlapping copy by byte
    }
}

bool InflateFixed(PngBitReader& in, std::vector<uint8_t>& out, std::string* error) {
    static const auto tables = [] {
        std::pair<PngHuffman, PngHuffman> t;
        uint8_t lengths[288];
        size_t i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        t.first.Build(lengths, 288);
        uint8_t dl[30];
        for (auto& l : dl) l = 5;
        t.second.Build(dl, 30);
        return t;
    }();
    return InflateCodes(in, tables.first, tables.second, out, error);
}

bool InflateDynamic(PngBitReader& in, std::vector<uint8_t>& out, std::string* error) {
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!in.Bits(5, hlit) || !in.Bits(5, hdist) || !in.Bits(4, hclen)) return Fail(error, "deflate: truncated header");
    const size_t nlen = hlit + 257, ndist = hdist + 1, ncode = hclen + 4;
    if (nlen > 286 || ndist > 30) return Fail(error, "deflate: bad code counts");
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t lengths[320] = {};
    for (size_t i = 0; i < ncode; ++i) {
        uint32_t v = 0;
        if (!in.Bits(3, v)) return Fail(error, "deflate: truncated code lengths");
        lengths[order[i]] = uint8_t(v);
    }
    PngHuffman lencode;
    if (!lencode.Build(lengths, 19)) return Fail(error, "deflate: bad code-length code");
    size_t index = 0;
    std::memset(lengths, 0, sizeof(lengths));
    while (index < nlen + ndist) {
        int symbol = 0;
        if (!lencode.Decode(in, symbol)) return Fail(error, "deflate: truncated lengths");
        if (symbol < 16) { lengths[index++] = uint8_t(symbol); continue; }
        uint8_t repeat_value = 0;
        uint32_t repeat = 0, extra = 0;
        if (symbol == 16) {
            if (index == 0) return Fail(error, "deflate: repeat with no previous length");
            repeat_value = lengths[index - 1];
            if (!in.Bits(2, extra)) return Fail(error, "deflate: truncated repeat");
            repeat = 3 + extra;
        } else if (symbol == 17) {
            if (!in.Bits(3, extra)) return Fail(error, "deflate: truncated repeat");
            repeat = 3 + extra;
        } else {
            if (!in.Bits(7, extra)) return Fail(error, "deflate: truncated repeat");
            repeat = 11 + extra;
        }
        if (index + repeat > nlen + ndist) return Fail(error, "deflate: too many lengths");
        while (repeat--) lengths[index++] = repeat_value;
    }
    if (lengths[256] == 0) return Fail(error, "deflate: no end-of-block code");
    PngHuffman lit, dist;
    if (!lit.Build(lengths, nlen)) return Fail(error, "deflate: bad literal/length code");
    if (!dist.Build(lengths + nlen, ndist)) return Fail(error, "deflate: bad distance code");
    return InflateCodes(in, lit, dist, out, error);
}

uint8_t Paeth(int a, int b, int c) {
    const int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return uint8_t(a);
    return pb <= pc ? uint8_t(b) : uint8_t(c);
}

}  // namespace

uint32_t Png::Crc32(const uint8_t* bytes, size_t size, uint32_t seed) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t c = seed ^ 0xffffffffu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ bytes[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
uint32_t Png::Adler32(const uint8_t* bytes, size_t size) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size;) {
        const size_t chunk = size - i < 5552 ? size - i : 5552;   // the largest run before a modulo
        for (size_t j = 0; j < chunk; ++j) { a += bytes[i + j]; b += a; }
        a %= 65521; b %= 65521;
        i += chunk;
    }
    return b << 16 | a;
}

bool Png::Inflate(const uint8_t* bytes, size_t size, std::vector<uint8_t>& out, std::string* error) {
    if (size < 6) return Fail(error, "zlib: stream too short");
    const uint8_t cmf = bytes[0], flg = bytes[1];
    if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 || (flg & 0x20))
        return Fail(error, "zlib: not a deflate stream (or a preset dictionary)");
    PngBitReader in(bytes + 2, size - 2 - 4);
    std::vector<uint8_t> result;
    for (;;) {
        uint32_t final = 0, type = 0;
        if (!in.Bits(1, final) || !in.Bits(2, type)) return Fail(error, "deflate: truncated block header");
        if (type == 0) {
            in.AlignByte();
            const uint8_t* p = in.Cursor();
            if (!in.Skip(4)) return Fail(error, "deflate: truncated stored block");
            const uint16_t len = uint16_t(p[0] | p[1] << 8), nlen = uint16_t(p[2] | p[3] << 8);
            if (uint16_t(~len) != nlen) return Fail(error, "deflate: stored length check failed");
            p = in.Cursor();
            if (!in.Skip(len)) return Fail(error, "deflate: truncated stored block");
            result.insert(result.end(), p, p + len);
        } else if (type == 1) {
            if (!InflateFixed(in, result, error)) return false;
        } else if (type == 2) {
            if (!InflateDynamic(in, result, error)) return false;
        } else return Fail(error, "deflate: reserved block type");
        if (final) break;
    }
    const uint8_t* tail = bytes + 2 + in.Position();
    if (size_t(tail - bytes) + 4 > size) return Fail(error, "zlib: missing Adler-32");
    if (Be32(tail) != Adler32(result.data(), result.size())) return Fail(error, "zlib: Adler-32 mismatch");
    out = std::move(result);
    return true;
}

bool Png::Decode(const uint8_t* bytes, size_t size, Image& out, std::string* error) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (!bytes || size < 8 + 25 || std::memcmp(bytes, signature, 8)) return Fail(error, "png: not a PNG signature");
    uint32_t width = 0, height = 0;
    unsigned depth = 0, colour = 0;
    bool header = false;
    std::vector<uint8_t> idat;
    std::vector<std::array<uint8_t, 4>> palette;
    size_t at = 8;
    while (at + 12 <= size) {
        const uint32_t length = Be32(bytes + at);
        if (length > size - at - 12) return Fail(error, "png: chunk past the end of file");
        const uint8_t* type = bytes + at + 4;
        const uint8_t* data = bytes + at + 8;
        if (Crc32(type, 4 + length) != Be32(data + length)) return Fail(error, "png: chunk CRC mismatch");
        if (!std::memcmp(type, "IHDR", 4)) {
            if (length != 13 || header) return Fail(error, "png: bad IHDR");
            width = Be32(data); height = Be32(data + 4);
            depth = data[8]; colour = data[9];
            if (data[10] || data[11]) return Fail(error, "png: unsupported compression/filter method");
            if (data[12]) return Fail(error, "png: interlaced (Adam7) images are not supported");
            if (!width || !height || width > kMaxSide || height > kMaxSide) return Fail(error, "png: unsupported size");
            const bool depth_ok = (colour == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                                  (colour == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                                  ((colour == 2 || colour == 4 || colour == 6) && (depth == 8 || depth == 16));
            if (!depth_ok) return Fail(error, "png: unsupported colour type / bit depth");
            header = true;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            if (!header || length % 3 || length > 768) return Fail(error, "png: bad PLTE");
            palette.resize(length / 3);
            for (size_t i = 0; i < palette.size(); ++i)
                palette[i] = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2], 255};
        } else if (!std::memcmp(type, "tRNS", 4)) {
            if (!header) return Fail(error, "png: tRNS before IHDR");
            if (colour == 3) {
                if (length > palette.size()) return Fail(error, "png: tRNS longer than PLTE");
                for (size_t i = 0; i < length; ++i) palette[i][3] = data[i];
            }
            // Single-colour transparency for grey/RGB is ignored: those images decode opaque.
        } else if (!std::memcmp(type, "IDAT", 4)) {
            if (!header) return Fail(error, "png: IDAT before IHDR");
            idat.insert(idat.end(), data, data + length);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        at += 12 + length;
    }
    if (!header || idat.empty()) return Fail(error, "png: no image data");
    if (colour == 3 && palette.empty()) return Fail(error, "png: palette image without PLTE");
    std::vector<uint8_t> raw;
    if (!Inflate(idat.data(), idat.size(), raw, error)) return false;
    const unsigned channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 3 ? 1 : colour == 4 ? 2 : 4;
    const size_t bits_per_pixel = size_t(channels) * depth;
    const size_t stride = (size_t(width) * bits_per_pixel + 7) / 8;
    const size_t bpp = bits_per_pixel < 8 ? 1 : bits_per_pixel / 8;   // the filter's byte step
    if (raw.size() != (stride + 1) * height) return Fail(error, "png: image data size disagrees with IHDR");
    // Unfilter in place (the previous row is already reconstructed).
    std::vector<uint8_t> zero(stride, 0);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = raw.data() + y * (stride + 1);
        const uint8_t filter = row[0];
        uint8_t* cur = row + 1;
        const uint8_t* prev = y ? raw.data() + (y - 1) * (stride + 1) + 1 : zero.data();
        if (filter > 4) return Fail(error, "png: unknown filter type");
        for (size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? cur[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
            switch (filter) {
                case 1: cur[x] = uint8_t(cur[x] + a); break;
                case 2: cur[x] = uint8_t(cur[x] + b); break;
                case 3: cur[x] = uint8_t(cur[x] + ((a + b) >> 1)); break;
                case 4: cur[x] = uint8_t(cur[x] + Paeth(a, b, c)); break;
                default: break;
            }
        }
    }
    Image image;
    image.width = width; image.height = height;
    image.rgba.resize(size_t(width) * height * 4);
    const unsigned max = (1u << depth) - 1;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* cur = raw.data() + y * (stride + 1) + 1;
        uint8_t* dst = image.rgba.data() + size_t(y) * width * 4;
        // One sample of `depth` bits at index i of the row (big-endian packing).
        const auto sample = [&](size_t i) -> unsigned {
            if (depth == 8) return cur[i];
            if (depth == 16) return cur[i * 2];   // high byte
            const size_t bit = i * depth;
            return (cur[bit / 8] >> (8 - depth - bit % 8)) & max;
        };
        const auto scale = [&](unsigned v) -> uint8_t { return depth >= 8 ? uint8_t(v) : uint8_t(v * 255 / max); };
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* px = dst + size_t(x) * 4;
            switch (colour) {
                case 0: { const uint8_t g = scale(sample(x)); px[0] = px[1] = px[2] = g; px[3] = 255; break; }
                case 2: px[0] = scale(sample(x * 3)); px[1] = scale(sample(x * 3 + 1)); px[2] = scale(sample(x * 3 + 2)); px[3] = 255; break;
                case 3: {
                    const unsigned index = sample(x);
                    if (index >= palette.size()) return Fail(error, "png: palette index out of range");
                    std::memcpy(px, palette[index].data(), 4);
                    break;
                }
                case 4: { const uint8_t g = scale(sample(x * 2)); px[0] = px[1] = px[2] = g; px[3] = scale(sample(x * 2 + 1)); break; }
                default: for (unsigned c = 0; c < 4; ++c) px[c] = scale(sample(x * 4 + c)); break;
            }
        }
    }
    out = std::move(image);
    if (error) error->clear();
    return true;
}

bool Png::DecodeFile(const std::string& path, Image& out, std::string* error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return Fail(error, "png: cannot open file");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return Decode(bytes.data(), bytes.size(), out, error);
}

std::vector<uint8_t> Png::Encode(const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> out;
    if (!rgba || !width || !height || width > kMaxSide || height > kMaxSide) return out;
    const size_t stride = size_t(width) * 4;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);   // filter: none
        raw.insert(raw.end(), rgba + y * stride, rgba + (y + 1) * stride);
    }
    std::vector<uint8_t> zlib{0x78, 0x01};   // CMF: deflate, 32 KiB window; FLG: no dictionary, level 0
    for (size_t at = 0; at < raw.size();) {
        const size_t chunk = raw.size() - at < 65535 ? raw.size() - at : 65535;
        const bool last = at + chunk == raw.size();
        zlib.push_back(last ? 1 : 0);   // BFINAL, BTYPE 00 (stored)
        zlib.push_back(uint8_t(chunk)); zlib.push_back(uint8_t(chunk >> 8));
        zlib.push_back(uint8_t(~chunk)); zlib.push_back(uint8_t(~chunk >> 8));
        zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + chunk);
        at += chunk;
    }
    PutBe32(zlib, Adler32(raw.data(), raw.size()));
    const auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        PutBe32(out, uint32_t(data.size()));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        PutBe32(out, Crc32(out.data() + start, out.size() - start));
    };
    out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    PutBe32(ihdr, width); PutBe32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});   // 8-bit RGBA, deflate, adaptive filters, no interlace
    chunk("IHDR", ihdr);
    chunk("IDAT", zlib);
    chunk("IEND", {});
    return out;
}

bool Png::EncodeFile(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height,
                     std::string* error) {
    const auto bytes = Encode(rgba, width, height);
    if (bytes.empty()) return Fail(error, "png: invalid image size");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return Fail(error, "png: cannot create file");
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!stream) return Fail(error, "png: write failed");
    if (error) error->clear();
    return true;
}

}  // namespace image
}  // namespace ps2r
