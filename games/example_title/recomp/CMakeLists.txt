# games/example_title/recomp -- the recompiled EE code.
#
# Nothing is committed here by design: the recompiled translation units are derived from the
# user's own ELF and are GENERATED into the build directory (cmake/TitleInputs.cmake writes
# ${PS2R_RECOMP_GEN_DIR}). A real title's CMakeLists defines ps2r_title_recomp over those
# generated sources, plus MANIFEST.sha256 so a regeneration can be proved identical.
#
# The template defines no target: every consumer gates on if(TARGET ps2r_title_recomp), so the
# recompiled-code tests and the play binary's EE half are simply skipped here.
message(STATUS "games/example_title: no recompiled EE code (template title; ps2r_title_recomp not defined)")
