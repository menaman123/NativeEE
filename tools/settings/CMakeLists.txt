# tools/settings -- nativeee_settings: the settings file (nativeee.ini) from the command
# line, through the engine's own ConfigStore / SettingsFile (the launcher's path into the
# store without linking the engine; the settings judge's "launcher writes" arm). Reads,
# writes (validated against the Config schema, atomic replace), lists, resolves the store
# the game would use for a given executable folder / data root, and runs the legacy
# migration. No window, no title.
add_executable(nativeee_settings nativeee_settings.cpp)
target_include_directories(nativeee_settings PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(nativeee_settings PRIVATE -Wall -Wextra -Werror)
target_link_libraries(nativeee_settings PRIVATE ps2r_core)
