# engine/title -- the title CONTRACT, title-neutral by construction. Header-only: the
# TitleProfile shape plus the ActiveTitleProfile() declaration the linked title module
# defines. This is what lets app/host name a title's retail facts without naming a title,
# so a build with PS2R_TITLE empty still compiles the composition root.
add_library(ps2r_title_api INTERFACE)
target_include_directories(ps2r_title_api INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(ps2r_title_api INTERFACE ps2r_ee)   # BootParams (engine/ee/hle/boot_env.h)
