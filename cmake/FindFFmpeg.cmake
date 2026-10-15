# cmake/FindFFmpeg.cmake -- locates the three FFmpeg components the ipu/hle video
# decoder links (libavcodec: MPEG-2 video decode; libavutil: frame / error
# helpers; libswscale: YUV -> RGBA) and exposes them as IMPORTED targets
#   FFmpeg::avcodec  FFmpeg::avutil  FFmpeg::swscale
# plus FFMPEG_INCLUDE_DIR. Homebrew's prefix is the hint; a system install on
# the default search path is found as well. Absence is fatal: the FMV path has
# no software fallback decoder.
#
# Included (not find_package'd) by engine/ipu/CMakeLists.txt so the root keeps
# no CMAKE_MODULE_PATH.

if(TARGET FFmpeg::avcodec)
    return()
endif()

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS /opt/homebrew/include /usr/local/include)

find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec HINTS /opt/homebrew/lib /usr/local/lib)
find_library(FFMPEG_AVUTIL_LIBRARY  NAMES avutil  HINTS /opt/homebrew/lib /usr/local/lib)
find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale HINTS /opt/homebrew/lib /usr/local/lib)

if(NOT FFMPEG_INCLUDE_DIR OR NOT FFMPEG_AVCODEC_LIBRARY OR
   NOT FFMPEG_AVUTIL_LIBRARY OR NOT FFMPEG_SWSCALE_LIBRARY)
    message(FATAL_ERROR
        "FFmpeg (libavcodec, libavutil, libswscale) not found. "
        "Install it with 'brew install ffmpeg' (macOS) or the distribution's "
        "libavcodec-dev / libavutil-dev / libswscale-dev packages. "
        "include=${FFMPEG_INCLUDE_DIR} avcodec=${FFMPEG_AVCODEC_LIBRARY} "
        "avutil=${FFMPEG_AVUTIL_LIBRARY} swscale=${FFMPEG_SWSCALE_LIBRARY}")
endif()

foreach(component avcodec avutil swscale)
    string(TOUPPER ${component} upper)
    add_library(FFmpeg::${component} UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::${component} PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_${upper}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
endforeach()

message(STATUS "nativeee: FFmpeg found (include=${FFMPEG_INCLUDE_DIR}, avcodec=${FFMPEG_AVCODEC_LIBRARY})")
