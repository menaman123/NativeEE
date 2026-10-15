# engine/cdvd -- the disc (PHASE4_PLAN section 2.5). Stage 4D: the ISO9660
# volume (IsoFs), the sector/file read service the IOP readers park on
# (ReadEngine = CdvdReader), the libcdvd SDK rows (LibcdvdBindings). Stage 4D': the
# sceCdSt* ring (StreamReader) and the completion-visibility meter (DeliveryMeter).
add_library(ps2r_cdvd STATIC
    iso_fs.cpp
    read_engine.cpp
    stream_reader.cpp
    delivery_meter.cpp
    delivery_budget.cpp
    ../services/read_timing.cpp
    hle/libcdvd_bindings.cpp
    hle/loader_model.cpp
)
target_include_directories(ps2r_cdvd PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_cdvd PRIVATE -Wall -Wextra -Werror)
# The CdvdReader interface the IOP readers park on lives here (cdvd_reader.h);
# the disc is the EE side's, so this library never links the IOP.
target_link_libraries(ps2r_cdvd PUBLIC ps2r_ee)
