#include "tests/cdvd/stream_library_fixture.h"

#include <cstring>
#include <filesystem>
#include <vector>
#include <unistd.h>
#include "engine/ee/recomp_abi/host_app/memory.h"

namespace stream_fixture {
uint8_t Byte(uint32_t offset) { return static_cast<uint8_t>((offset * 23 + offset / 251 + 19) & 255); }
void Text(uint32_t at, const std::string& text) {
    for (size_t i = 0; i <= text.size(); ++i) memory::write<uint8_t>(at + i, text.c_str()[i]);
}
void Fill(uint32_t at, uint32_t size, uint8_t value) {
    for (uint32_t i = 0; i < size; ++i) memory::write<uint8_t>(at + i, value);
}
bool Bytes(uint32_t at, uint32_t offset, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) if (memory::read<uint8_t>(at + i) != Byte(offset + i)) return false;
    return true;
}
void Word(uint8_t* at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) { at[i] = value >> (i * 8); at[7 - i] = at[i]; }
}
size_t Entry(uint8_t* at, const std::string& name, uint32_t lba, uint32_t size, bool directory = false) {
    const size_t bytes = (33 + name.size() + 1) & ~size_t(1);
    at[0] = bytes; Word(at + 2, lba); Word(at + 10, size);
    at[25] = directory ? 2 : 0; at[28] = at[31] = 1;
    at[32] = name.size(); std::memcpy(at + 33, name.data(), name.size());
    return bytes;
}
std::string Disc() {
    std::vector<uint8_t> bytes(25 * 2048);
    auto* pvd = bytes.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    Word(pvd + 80, 25); pvd[128] = pvd[131] = 0; pvd[129] = pvd[130] = 8;
    Entry(pvd + 156, std::string(1, '\0'), 19, 2048, true);
    auto* end = bytes.data() + 17 * 2048;
    end[0] = 255; std::memcpy(end + 1, "CD001", 5); end[6] = 1;
    auto* dir = bytes.data() + 19 * 2048;
    size_t n = Entry(dir, std::string(1, '\0'), 19, 2048, true);
    n += Entry(dir + n, std::string(1, '\1'), 19, 2048, true);
    n += Entry(dir + n, "DATA.BD;1", 20, kSize);
    n += Entry(dir + n, "MUSIC.MB;1", 20, kSize);
    Entry(dir + n, "SHORT.BH;1", 24, kSize);
    for (uint32_t i = 0; i < kSize; ++i) bytes[20 * 2048 + i] = Byte(i);
    std::string path = (std::filesystem::temp_directory_path() / "stream_library_XXXXXX").string();
    const int fd = mkstemp(path.data());
    if (fd < 0) return {};
    const auto wrote = write(fd, bytes.data(), bytes.size());
    close(fd);
    if (wrote != static_cast<ssize_t>(bytes.size())) { unlink(path.c_str()); return {}; }
    return path;
}
}  // namespace stream_fixture
