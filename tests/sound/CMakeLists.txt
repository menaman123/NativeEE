# tests/sound -- Phase 4 SPU2 suites (PHASE4_PLAN section 2.7 / 4.1). Stage 4E':
# the VagDecoder vectors (the original tests/test_vag_decoder.cpp, ported).
add_executable(test_vag_decoder test_vag_decoder.cpp)
target_link_libraries(test_vag_decoder PRIVATE ps2r_sound)
target_compile_options(test_vag_decoder PRIVATE -Wall -Wextra -Werror)
add_test(NAME VagDecoder COMMAND test_vag_decoder)
set_tests_properties(VagDecoder PROPERTIES TIMEOUT 60)

# Stage 4E'': the voice vectors (the original tests/test_sound_voice.cpp, ported)
# and the tick-clocked pump (silence window hash, cadence, selftest tone, ring).
add_executable(test_sound_voice test_sound_voice.cpp)
target_link_libraries(test_sound_voice PRIVATE ps2r_sound)
target_compile_options(test_sound_voice PRIVATE -Wall -Wextra -Werror)
add_test(NAME SoundVoice COMMAND test_sound_voice)
set_tests_properties(SoundVoice PROPERTIES TIMEOUT 60)

add_executable(test_mixer_tick_pump test_mixer_tick_pump.cpp mixer_voice_source.cpp)
target_link_libraries(test_mixer_tick_pump PRIVATE ps2r_sound ps2r_core)
target_compile_options(test_mixer_tick_pump PRIVATE -Wall -Wextra -Werror)
add_test(NAME MixerTickPump COMMAND test_mixer_tick_pump)
set_tests_properties(MixerTickPump PROPERTIES TIMEOUT 60)

add_executable(test_audio_mixer test_audio_mixer.cpp mixer_voice_source.cpp pcm_source_fixture.cpp)
target_link_libraries(test_audio_mixer PRIVATE ps2r_sound)
target_compile_options(test_audio_mixer PRIVATE -Wall -Wextra -Werror)
add_test(NAME AudioMixer COMMAND test_audio_mixer)
set_tests_properties(AudioMixer PROPERTIES TIMEOUT 60)

# Stage 4E'' (iteration 2): the libsd bodies over SpuCore (VoiceTrans bytes, the
# deferred handler, the real ProcBatch) and the libsdr surface (SdrRemote face,
# SdBlockTrans on the mixer clock, the manifest rows).
add_executable(test_libsd_module test_libsd_module.cpp)
target_link_libraries(test_libsd_module PRIVATE ps2r_iop ps2r_sound_hle)
target_compile_options(test_libsd_module PRIVATE -Wall -Wextra -Werror)
add_test(NAME LibsdModule COMMAND test_libsd_module)
set_tests_properties(LibsdModule PROPERTIES TIMEOUT 60)

# Stage 4E''': the REALTIME-policy hammer -- a producer thread on the libsd
# surface against the mixer thread, bounded deadline (CommandRing over
# core/threads CmdRing, atomic readbacks).
add_executable(test_spu_core_hammer test_spu_core_hammer.cpp)
target_link_libraries(test_spu_core_hammer PRIVATE ps2r_sound ps2r_core)
target_compile_options(test_spu_core_hammer PRIVATE -Wall -Wextra -Werror)
add_test(NAME SpuCoreHammer COMMAND test_spu_core_hammer)
set_tests_properties(SpuCoreHammer PROPERTIES TIMEOUT 60)

# Stage 4E''': the SNDC/SDRX savestate codec round trip (the original
# tests/test_sound_state.cpp core vectors, ported) + the SectionRegistry path.
add_executable(test_sound_state test_sound_state.cpp)
target_link_libraries(test_sound_state PRIVATE ps2r_sound_hle ps2r_core)
target_compile_options(test_sound_state PRIVATE -Wall -Wextra -Werror)
add_test(NAME SoundState COMMAND test_sound_state)
set_tests_properties(SoundState PROPERTIES TIMEOUT 60)

# Phase 7 integration (row 7.2): the mixer's FMV PCM source -- the FIFO rules
# (gate, drop-newest, flush) and the MixInto rule (after the voices at the
# core-0 BVOLL/BVOLR gain, before the master).
add_executable(test_fmv_pcm_input test_fmv_pcm_input.cpp)
target_link_libraries(test_fmv_pcm_input PRIVATE ps2r_sound)
target_compile_options(test_fmv_pcm_input PRIVATE -Wall -Wextra -Werror)
add_test(NAME FmvPcmInput COMMAND test_fmv_pcm_input)
set_tests_properties(FmvPcmInput PROPERTIES TIMEOUT 60)
