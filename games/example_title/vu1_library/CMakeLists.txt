# games/example_title/vu1_library -- the VU1 HLE program library.
#
# The engine's VU1 mechanism is generic: it hashes the microprogram the title uploads and looks
# it up in a per-title registry. The CONTENT is title data -- one native routine per program,
# keyed by that hash. Without an entry the microprogram is interpreted, which is correct but
# far slower than the native routine (README.md, "Per-title HLE").
#
# The template ships no programs and defines no target (consumers gate on
# if(TARGET ps2r_title_vu1_library)), so every VU1 program falls back to interpretation.
message(STATUS "games/example_title: no VU1 program library (template title; every program interprets)")
