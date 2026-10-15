// engine/ipu/hle/pss_demux.cpp -- PssDemux packet walk (see pss_demux.h).
#include "engine/ipu/hle/pss_demux.h"

#include <algorithm>

namespace ps2r::ipu {

namespace {

uint16_t ReadBe16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

}  // namespace

PssDemux::PssDemux(PssDemuxListener& listener) : listener_(listener) {}

void PssDemux::Append(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;
    buffer_.insert(buffer_.end(), data, data + size);
}

void PssDemux::Reset() {
    buffer_.clear();
    ended_ = false;
    consumed_ = 0;
    packs_ = 0;
    system_headers_ = 0;
}

void PssDemux::MarkEnded() { ended_ = true; }

size_t PssDemux::FindStartCode(const std::vector<uint8_t>& buf, size_t from) {
    if (buf.size() < 4 || from + 3 >= buf.size()) return kStartCodeNotFound;
    for (size_t i = from; i + 3 < buf.size(); ++i) {
        if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) return i;
    }
    return kStartCodeNotFound;
}

bool PssDemux::ContainsSequenceEnd(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;
    for (size_t i = 0; i + 3 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01 &&
            data[i + 3] == kSequenceEnd) {
            return true;
        }
    }
    return false;
}

// 13818-1 stream_id ranges: 0xE0..0xEF video, 0xC0..0xDF MPEG audio; the
// private_stream_1 id 0xBD carries this container's PCM and is treated as audio.
bool PssDemux::IsVideoStreamId(uint8_t id) { return id >= 0xE0 && id <= 0xEF; }
bool PssDemux::IsAudioStreamId(uint8_t id) { return id == 0xBD || (id >= 0xC0 && id <= 0xDF); }

// system_header, program_stream_map, padding, private_stream_2, ECM/EMM,
// DSMCC, ITU-T H.222.1 type E and program_stream_directory: 16-bit length
// prefixed, skipped whole.
bool PssDemux::IsLengthPrefixedHeader(uint8_t id) {
    switch (id) {
    case kSystemHeader:
    case 0xBC: case 0xBE: case 0xBF:
    case 0xF0: case 0xF1: case 0xF2: case 0xF8: case 0xFF:
        return true;
    default:
        return false;
    }
}

size_t PssDemux::ParsePesPayloadOffset(const uint8_t* packet, size_t packet_size) {
    if (!packet || packet_size <= 6) return packet_size;

    size_t pos = 6;
    // MPEG-2 PES header: '10' marker in the first header byte, then flags and
    // PES_header_data_length at +8; the payload follows the header data.
    if (packet_size >= 9 && (packet[pos] & 0xC0) == 0x80) {
        return std::min(packet_size, 9 + static_cast<size_t>(packet[pos + 2]));
    }

    // MPEG-1 PES header: stuffing bytes (0xFF), optional STD buffer field
    // ('01' marker, 2 bytes), then PTS (5 bytes) / PTS+DTS (10 bytes) / the
    // 0x0F "no timestamp" byte.
    while (pos < packet_size && packet[pos] == 0xFF) ++pos;

    if (pos + 1 < packet_size && (packet[pos] & 0xC0) == 0x40) pos += 2;
    if (pos >= packet_size) return packet_size;

    const uint8_t flags = packet[pos];
    if      ((flags & 0xF0) == 0x20) pos += 5;
    else if ((flags & 0xF0) == 0x30) pos += 10;
    else if (flags == 0x0F)          pos += 1;

    return std::min(packet_size, pos);
}

void PssDemux::ErasePrefix(size_t n, bool walked) {
    const size_t clamped = std::min(n, buffer_.size());
    if (clamped == 0) return;
    if (walked) consumed_ += clamped;
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(clamped));
}

void PssDemux::Process(bool final_chunk) {
    auto& buf = buffer_;
    for (;;) {
        if (ended_) {
            // Nothing after the end code is meaningful input.
            ErasePrefix(buf.size(), /*walked=*/false);
            return;
        }

        const size_t start = FindStartCode(buf, 0);
        if (start == kStartCodeNotFound) {
            if (final_chunk)         ErasePrefix(buf.size());
            else if (buf.size() > 3) ErasePrefix(buf.size() - 3);
            return;
        }
        if (start > 0) ErasePrefix(start);
        if (buf.size() < 4) return;

        const uint8_t stream_id = buf[3];

        if (stream_id == kProgramEnd) {
            ended_ = true;
            listener_.OnProgramEnd();
            ErasePrefix(4);
            ErasePrefix(buf.size(), /*walked=*/false);
            return;
        }

        // pack_header: 12 bytes in MPEG-1 ('0010' marker at +4), 14 bytes plus
        // pack_stuffing_length (low 3 bits of +13) in MPEG-2 ('01' marker).
        if (stream_id == kPackHeader) {
            if (buf.size() < 12) {
                if (final_chunk) ErasePrefix(buf.size());
                return;
            }
            size_t pack_size = 12;
            if ((buf[4] & 0xC0) == 0x40) {
                if (buf.size() < 14) {
                    if (final_chunk) ErasePrefix(buf.size());
                    return;
                }
                pack_size = 14 + static_cast<size_t>(buf[13] & 0x07);
            }
            if (buf.size() < pack_size) {
                if (final_chunk) ErasePrefix(buf.size());
                return;
            }
            ++packs_;
            ErasePrefix(pack_size);
            continue;
        }

        if (buf.size() < 6) {
            if (final_chunk) ErasePrefix(buf.size());
            return;
        }
        const uint16_t pkt_len = ReadBe16(buf.data() + 4);

        if (IsLengthPrefixedHeader(stream_id)) {
            const size_t end = 6 + static_cast<size_t>(pkt_len);
            if (buf.size() < end) {
                if (final_chunk) ErasePrefix(buf.size());
                return;
            }
            ++system_headers_;
            ErasePrefix(end);
            continue;
        }

        // PES packet: PES_packet_length known, or 0 (unbounded: the packet
        // runs to the next start code, or to the end on the final chunk).
        size_t pkt_end = 0;
        if (pkt_len != 0) {
            pkt_end = 6 + static_cast<size_t>(pkt_len);
            if (buf.size() < pkt_end) {
                if (!final_chunk) return;
                pkt_end = buf.size();
            }
        } else {
            const size_t next = FindStartCode(buf, 6);
            if (next == kStartCodeNotFound) {
                if (!final_chunk) return;
                pkt_end = buf.size();
            } else {
                pkt_end = next;
            }
        }

        if (IsVideoStreamId(stream_id)) {
            const size_t payload = ParsePesPayloadOffset(buf.data(), pkt_end);
            if (payload < pkt_end) {
                const bool has_end = ContainsSequenceEnd(buf.data() + payload, pkt_end - payload);
                if (has_end) ended_ = true;
                listener_.OnVideoEs(buf.data() + payload, pkt_end - payload, has_end);
            }
        } else if (IsAudioStreamId(stream_id)) {
            const size_t payload = ParsePesPayloadOffset(buf.data(), pkt_end);
            if (payload < pkt_end) {
                listener_.OnAudioPes(buf.data() + payload, pkt_end - payload);
            }
        }

        ErasePrefix(pkt_end);
    }
}

}  // namespace ps2r::ipu
