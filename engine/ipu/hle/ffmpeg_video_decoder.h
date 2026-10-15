// engine/ipu/hle/ffmpeg_video_decoder.h -- FfmpegVideoDecoder: the VideoDecoder
// over libavcodec's MPEG-2 video decoder. An elementary stream is fed through an
// AVCodecParserContext (packet boundaries), each packet through the decoder on the
// calling thread (thread_count = 1: decode order equals serve order, no worker
// pool), and every produced frame is converted to RGBA8 with libswscale at the
// picture's own size. Only this .cpp includes FFmpeg headers; the class is
// move-only and owned by one MpegContext.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "engine/ipu/hle/decoded_picture.h"
#include "engine/ipu/hle/video_decoder.h"

struct AVCodecParserContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace ps2r::ipu {

class FfmpegVideoDecoder : public VideoDecoder {
public:
    FfmpegVideoDecoder() = default;
    ~FfmpegVideoDecoder() override;

    FfmpegVideoDecoder(const FfmpegVideoDecoder&) = delete;
    FfmpegVideoDecoder& operator=(const FfmpegVideoDecoder&) = delete;

    // VideoDecoder.
    void Feed(const uint8_t* data, size_t size, std::deque<DecodedPicture>& out) override;
    void Flush(std::deque<DecodedPicture>& out) override;
    double AuthoredFps() const override { return authored_fps_; }
    void Shutdown() override;

    // Pictures produced since construction (the count survives Shutdown).
    uint64_t FramesDecoded() const { return frames_decoded_; }

    // The linked libavcodec as "MAJOR.MINOR.MICRO" (the decoded RGBA bytes
    // depend on it: the per-picture golden records this string).
    static std::string LibraryVersion();
    static unsigned LibavcodecVersion();

private:
    bool EnsureInitialized();
    bool SendPacket(const uint8_t* data, int size, std::deque<DecodedPicture>& out);
    bool ReceiveFrames(std::deque<DecodedPicture>& out);
    bool ConvertFrame(std::deque<DecodedPicture>& out);

    AVCodecParserContext* parser_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    uint64_t frames_decoded_ = 0;
    double authored_fps_ = 0.0;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    int sws_w_ = 0;
    int sws_h_ = 0;
    int sws_fmt_ = -1;  // AVPixelFormat of the current SwsContext (-1 = AV_PIX_FMT_NONE)
    bool initialized_ = false;
    bool drained_ = false;
};

}  // namespace ps2r::ipu
