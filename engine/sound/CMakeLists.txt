# engine/sound -- the SPU2 half (PHASE4_PLAN section 2.7). Stage 4E': the ADPCM
# block decoder (VagDecoder); 4E'': Voice, SpuCore, CommandRing, Mixer (the
# tick-clocked pump); Phase 7 integration: FmvPcmInput (the mixer's FMV PCM
# source, the sound module's FmvPcmSink). The libsd bodies live in
# engine/iop/hle/libsd over SpuCore.
add_library(ps2r_sound STATIC
    vag_decoder.cpp
    voice.cpp
    command_ring.cpp
    spu_core.cpp
    mixer.cpp
    audio_mixer.cpp
    fmv_pcm_input.cpp
    voice_trace.cpp
)
# The libsdr surface (4E''): the SDRDRV face and the EE rows over SpuCore. Links
# the manifest (ps2r_ee) and the RPC router (ps2r_sif); ps2r_sound itself stays
# free of both so the IOP-side libsd bodies can link it alone.
add_library(ps2r_sound_hle STATIC
    sound_state_codec.cpp
    hle/sd_block_trans.cpp
    hle/sdr_remote.cpp
    hle/libsdr_bindings.cpp
)
target_include_directories(ps2r_sound_hle PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_sound_hle PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sound_hle PUBLIC ps2r_sound ps2r_ee ps2r_sif)
target_include_directories(ps2r_sound PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_sound PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sound PUBLIC ps2r_core)
