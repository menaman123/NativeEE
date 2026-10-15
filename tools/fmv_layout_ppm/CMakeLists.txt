# tools/fmv_layout_ppm -- lays a P6 PPM (the FmvPictureOracle --dump-dir picture) out
# through the REAL engine FrameWriter::WriteRgb32Strips into the RGB32 strip image the
# title's IPU frame plane holds. The plane-check procedure (verify/fmv_plane_check.py)
# compares these images strip-wise against a leg's state images.
add_executable(fmv_layout_ppm fmv_layout_ppm.cpp)
target_compile_options(fmv_layout_ppm PRIVATE -Wall -Wextra -Werror -O2)
target_link_libraries(fmv_layout_ppm PRIVATE ps2r_ipu)
