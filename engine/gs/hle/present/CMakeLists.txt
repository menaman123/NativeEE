# engine/gs/hle/present -- CRTC classifier, viewport math, scanout state + the
# display grab + the display quad (from the target or the mirror decode), and the
# presenter's per-VSync order. Backend-only GPU access.
add_library(ps2r_gs_hle_present STATIC
    crtc_classifier.cpp
    viewport_math.cpp
    scanout_composer.cpp
    presenter.cpp
    present_policy.cpp)
target_include_directories(ps2r_gs_hle_present PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_present PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_present PUBLIC ps2r_gs_hle_targets ps2r_gs_hle_texsrc)

# The display-shape selection (video.aspect): shared by the title's aspect model
# and the session presenter, which links no other GS library.
add_library(ps2r_gs_aspect STATIC aspect_selection.cpp)
target_include_directories(ps2r_gs_aspect PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_aspect PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_present PUBLIC ps2r_gs_aspect)
