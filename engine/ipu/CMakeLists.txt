# engine/ipu -- the EE Image Processing Unit: register model + MPEG intra decode
# arithmetic (PHASE7_PLAN section 2.1) and the sceMpeg / libipu HLE pipeline under
# hle/ (section 2.2). Depends on ps2r_core for the logger; the MmioHandler
# interface is the header engine/ee/mem/mmio_handler.h only (no link against
# ps2r_ee). FFmpeg (libavcodec / libavutil / libswscale) backs the hle video
# decoder; its headers are included by hle/ffmpeg_video_decoder.cpp only. The
# two SDK binding TUs (hle/lib{ipu,mpeg}_bindings.cpp) register rows with the
# SdkBindingManifest, which is ps2r_ee's -- the only engine/ee link here; the
# adapters (guest memory, DMAC face, sector source) take callbacks / register
# PODs so no other module is linked.
#
# M12 ledger L7: TWO libraries. ps2r_ipu is the LIVE movie path the play binary
# links (the sceMpeg rows over MpegApi, the FFmpeg decoder, PSS demux, pacer,
# audio capture, frame writer, the guest-memory / sector / PCM adapters).
# ps2r_ipu_model is the IPU MODEL the play census shows at zero (ipu_commands,
# ipu_operations, libipu_calls all 0: the owned movie decoder serves every FMV):
# the IPU device (register window 0x10002000, FIFOs, BCLR/IDEC/BDEC/VDEC/FDEC/
# SETIQ... + the intra decode arithmetic), the DMAC ch3/ch4 face, the libipu
# SDK rows (sceIpuInit/Sync/StopDMA/RestartDMA) and the retained sceMpegInit
# initialisation. Part of ps2r_iop_models: the ORACLE binary and the component
# oracles link it; the play binary does not (tests/lint/lint_play_binary_no_iop.py).
include(${CMAKE_SOURCE_DIR}/cmake/FindFFmpeg.cmake)

add_library(ps2r_ipu STATIC
    hle/pss_demux.cpp
    hle/frame_writer.cpp
    hle/fmv_pacer.cpp
    hle/fmv_audio_capture.cpp
    hle/ffmpeg_video_decoder.cpp
    hle/mpeg_context.cpp
    hle/mpeg_api.cpp
    hle/mpeg_api_state.cpp
    hle/guest_memory_adapter.cpp
    hle/fmv_sector_source_adapter.cpp
    hle/fmv_pcm_tally_sink.cpp
    hle/libmpeg_bindings.cpp)
target_include_directories(ps2r_ipu PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_ipu PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_ipu
    PUBLIC ps2r_core ps2r_ee
    PRIVATE FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)

add_library(ps2r_ipu_model STATIC
    ipu_device.cpp
    ipu_fifo_port.cpp
    ipu_color.cpp
    ipu_vlc.cpp
    mpeg_decode.cpp
    mpeg_coeff_vlc.cpp
    hle/libipu_api.cpp
    hle/dmac_channel_face_adapter.cpp
    hle/libipu_bindings.cpp
    hle/libmpeg_bindings_retained.cpp)
target_include_directories(ps2r_ipu_model PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_ipu_model PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_ipu_model PUBLIC ps2r_ipu ps2r_core ps2r_ee)
