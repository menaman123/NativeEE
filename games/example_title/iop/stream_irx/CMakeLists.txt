# games/example_title/iop/stream_irx -- the title's custom IOP module, recompiled.
#
# Most titles ship at least one custom .IRX that the stock IOP modules cannot stand in for.
# Like the EE code it is derived from the user's disc and generated into the build directory
# (${PS2R_IOP_GEN_DIR}); a real title defines ps2r_title_stream_irx over it.
#
# The template defines no target: consumers gate on if(TARGET ps2r_title_stream_irx).
message(STATUS "games/example_title: no recompiled IOP module (template title; ps2r_title_stream_irx not defined)")
