# games/example_title/renderer_profile -- the GS class-renderer profile.
#
# The class-renderer framework is generic; the profile is title data: which draw shapes this
# title emits, which native renderer accepts each, and the title's VRAM layout. Without a
# profile the GS register stream is replayed through the software path -- correct, and slower.
#
# The template ships no profile and defines no target (consumers gate on
# if(TARGET ps2r_title_renderer_profile)).
message(STATUS "games/example_title: no renderer profile (template title; draws take the generic path)")
