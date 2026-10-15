#pragma once

#include <cstdint>
#include <string>

namespace stream_fixture {
constexpr uint32_t kFs = 0x510000, kFs2 = 0x510100, kCb = 0x511000, kVtbl = 0x512000,
    kRing = 0x520000, kDest = 0x530000, kPath = 0x540000, kStatus = 0x550000, kCopy = 0x560000;
constexpr uint32_t kStart = 0x2ad9a8, kPoll = 0x2adaf0, kWait = 0x2ada30, kBank = 0x2ada68;
constexpr uint32_t kSize = 6003;

uint8_t Byte(uint32_t offset);
void Text(uint32_t at, const std::string& text);
void Fill(uint32_t at, uint32_t size, uint8_t value = 0xCE);
bool Bytes(uint32_t at, uint32_t offset, uint32_t size);
std::string Disc();
}  // namespace stream_fixture
