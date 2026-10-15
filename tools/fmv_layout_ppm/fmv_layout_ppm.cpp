// tools/fmv_layout_ppm/fmv_layout_ppm.cpp -- fmv_layout_ppm: read a P6 PPM into a
// DecodedPicture and lay it out with the engine's FrameWriter::WriteRgb32Strips (the
// RGB32 strip layout the libmpeg frame writer leaves in the title's image plane:
// 16-pixel column strips of macroblock-aligned rows, alpha 0x80, black outside the
// picture). Writes the strip bytes to <out.bin>; prints the dimensions and size.
//   fmv_layout_ppm <in.ppm> <out.bin>
// Exit 0 on success, 1 on a read/write error, 2 on usage.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/ipu/hle/decoded_picture.h"
#include "engine/ipu/hle/frame_writer.h"

namespace {

bool ReadPpm(const char* path, ps2r::ipu::DecodedPicture& pic) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::perror(path);
        return false;
    }
    char magic[3] = {0};
    int w = 0, h = 0, maxv = 0;
    if (std::fscanf(f, "%2s %d %d %d", magic, &w, &h, &maxv) != 4 || std::string(magic) != "P6" || maxv != 255 ||
        w <= 0 || h <= 0) {
        std::fprintf(stderr, "%s: not a P6 PPM with maxval 255\n", path);
        std::fclose(f);
        return false;
    }
    std::fgetc(f);  // the single whitespace byte after maxval
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    const size_t got = std::fread(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    if (got != rgb.size()) {
        std::fprintf(stderr, "%s: short PPM payload (%zu of %zu bytes)\n", path, got, rgb.size());
        return false;
    }
    pic.width = w;
    pic.height = h;
    pic.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i) {
        pic.rgba[i * 4 + 0] = rgb[i * 3 + 0];
        pic.rgba[i * 4 + 1] = rgb[i * 3 + 1];
        pic.rgba[i * 4 + 2] = rgb[i * 3 + 2];
        pic.rgba[i * 4 + 3] = 0xFF;  // the decoder's alpha; the writer stores 0x80
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fmv_layout_ppm <in.ppm> <out.bin>\n");
        return 2;
    }
    ps2r::ipu::DecodedPicture pic;
    if (!ReadPpm(argv[1], pic)) return 1;
    const size_t bytes = ps2r::ipu::FrameWriter::Rgb32Bytes(static_cast<uint32_t>(pic.width),
                                                            static_cast<uint32_t>(pic.height));
    std::vector<uint8_t> out(bytes, 0);
    ps2r::ipu::FrameWriter::WriteRgb32Strips(out.data(), pic);
    FILE* o = std::fopen(argv[2], "wb");
    if (!o) {
        std::perror(argv[2]);
        return 1;
    }
    const size_t put = std::fwrite(out.data(), 1, out.size(), o);
    std::fclose(o);
    if (put != out.size()) {
        std::fprintf(stderr, "%s: short write\n", argv[2]);
        return 1;
    }
    std::printf("%s: %dx%d -> %zu strip bytes\n", argv[1], pic.width, pic.height, bytes);
    return 0;
}
