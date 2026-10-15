# tools/drawlist_tool -- CPU-only inspector / differ for .gsdl recordings (stats,
# frames, dump, diff). Output grammar is the corpus-pinned one; tests/gs/ir compares
# it against the frozen OLD tool on the corpus.
add_executable(drawlist_tool drawlist_tool.cpp)
target_compile_options(drawlist_tool PRIVATE -Wall -Wextra -Werror -O2)
target_link_libraries(drawlist_tool PRIVATE ps2r_gsdl_reader)
