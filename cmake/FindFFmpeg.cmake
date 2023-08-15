find_path(FFmpeg_INCLUDE_DIRS NAMES libavcodec/avcodec.h libavformat/avformat.h
    PATHS ${FFMPEG_PATH}/include
    NO_DEFAULT_PATH)

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FFMPEG_LIB_SUFFIX x64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(FFMPEG_LIB_SUFFIX x86)
    endif()
elseif(ANDROID)
    set(FFMPEG_LIB_SUFFIX ${CMAKE_ANDROID_ARCH_ABI})
endif()

find_library(
    FFmpeg_LIBS
    NAMES ffmpeg
    PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
    NO_DEFAULT_PATH)

if (NOT FFmpeg_INCLUDE_DIRS OR NOT FFmpeg_LIBS)
    message(FATAL_ERROR "Could not find ffmpeg library")
else()
    message(STATUS "Found FFMpeg: ${FFmpeg_INCLUDE_DIRS} ${FFmpeg_LIBS}")
endif()

set(FFmpeg_INCLUDE_DIRS "${FFmpeg_INCLUDE_DIRS}" CACHE PATH "Path to ffmpeg include directory")
set(FFmpeg_LIBS "${FFmpeg_LIBS}" CACHE PATH "Path to ffmpeg libs")
mark_as_advanced(FFmpeg_INCLUDE_DIRS FFmpeg_LIBS)
