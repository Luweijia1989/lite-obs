find_path(FFmpeg_INCLUDE_DIRS NAMES libavcodec/avcodec.h libavformat/avformat.h PATHS ${FFmpegPath}/include)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FFMPEG_LIB_SUFFIX x64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(FFMPEG_LIB_SUFFIX x86)
endif()

find_library(
    FFmpeg_LIBS
    NAMES ffmpeg
    PATHS ${FFmpegPath}/lib/${FFMPEG_LIB_SUFFIX})

if (NOT FFmpeg_INCLUDE_DIRS OR NOT FFmpeg_LIBS)
    message(FATAL_ERROR "Could not find ffmpeg library")
else()
    message(STATUS "Found FFMpeg: ${FFmpeg_INCLUDE_DIRS}")
endif()

set(FFmpeg_INCLUDE_DIRS "${FFmpeg_INCLUDE_DIRS}" CACHE PATH "Path to ffmpeg include directory")
set(FFmpeg_LIBS "${FFmpeg_LIBS}" CACHE PATH "Path to ffmpeg libs")
mark_as_advanced(FFmpeg_INCLUDE_DIRS FFmpeg_LIBS)
