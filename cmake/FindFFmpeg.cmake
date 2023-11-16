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

if(MSVC OR ANDROID)
    find_library(
        FFmpeg_LIBS
        NAMES ffmpeg
        PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
        NO_DEFAULT_PATH)

    if(MSVC)
        find_file(FFmpeg_BIN_FILE NAMES ffmpeg-6.dll ffmpeg.dll
            PATHS ${FFMPEG_PATH}/bin/${FFMPEG_LIB_SUFFIX}
            NO_DEFAULT_PATH)
    else()
        set(FFmpeg_BIN_FILE ${FFmpeg_LIBS})
    endif()
else()
    set(FFMPEG_DEP_LIST "libavcodec.a" "libavformat.a" "libavutil.a" "libswresample.a" "libswscale.a")
    foreach(library ${FFMPEG_DEP_LIST})
        find_library(
            FOUND_LIBRARY_${library}
            NAMES ${library}
            PATHS ${FFMPEG_PATH}/lib/${FFMPEG_LIB_SUFFIX}
            NO_DEFAULT_PATH)

        if(FOUND_LIBRARY_${library})
            list(APPEND FFmpeg_LIBS ${FOUND_LIBRARY_${library}})
        endif()
    endforeach()
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
