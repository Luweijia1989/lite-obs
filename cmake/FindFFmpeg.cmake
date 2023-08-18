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

if(MSVC)
    find_library(
        FFmpeg_LIBS
        NAMES ffmpeg
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    find_file(FFmpeg_BIN_FILE NAMES ffmpeg-6.dll ffmpeg.dll
        PATHS ${FFMPEG_PATH}/bin/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)
else()
    find_library(
        AVCODEC
        NAMES libavcodec.a
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    find_library(
        AVFORMAT
        NAMES libavformat.a
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    find_library(
        AVUTIL
        NAMES libavutil.a
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    find_library(
        SWRESAMPLE
        NAMES libswresample.a
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    find_library(
        SWSCALE
        NAMES libswscale.a
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    if (AVCODEC AND AVFORMAT AND AVUTIL AND SWRESAMPLE AND SWSCALE)
        set(FFmpeg_LIBS ${AVCODEC} ${AVFORMAT} ${SWRESAMPLE} ${AVUTIL} ${SWSCALE})
    endif()

    if (APPLE)
        find_library(
            X264
            NAMES libx264.a
            PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
            NO_DEFAULT_PATH)

        find_library(
            X265
            NAMES libx265.a
            PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
            NO_DEFAULT_PATH)

        if (X264 AND X265)
            set(FFmpeg_LIBS ${FFmpeg_LIBS} ${X264} ${X265})
        endif()
    endif()
endif()

if (NOT FFmpeg_INCLUDE_DIRS OR NOT FFmpeg_LIBS)
    message(FATAL_ERROR "Could not find ffmpeg library")
else()
    message(STATUS "Found FFMpeg: ${FFmpeg_INCLUDE_DIRS} ${FFmpeg_LIBS}")
endif()

set(FFmpeg_INCLUDE_DIRS "${FFmpeg_INCLUDE_DIRS}" CACHE PATH "Path to ffmpeg include directory")
set(FFmpeg_LIBS "${FFmpeg_LIBS}" CACHE PATH "Path to ffmpeg libs")
set(FFmpeg_BIN_FILE "${FFmpeg_BIN_FILE}" CACHE PATH "Path to ffmpeg dlls")
mark_as_advanced(FFmpeg_INCLUDE_DIRS FFmpeg_LIBS FFmpeg_BIN_FILE)
