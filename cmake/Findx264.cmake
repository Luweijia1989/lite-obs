if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(SUFFIX "debug")
endif()

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH x64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(ARCH x86)
    endif()
elseif(ANDROID)
    set(ARCH ${CMAKE_ANDROID_ARCH_ABI})
endif()

find_path(X264_INCLUDE_DIRS NAMES x264.h
    PATHS ${THIRDPARTY_PATH}
    PATH_SUFFIXES ${ARCH}/include
    NO_DEFAULT_PATH)

find_library(
    X264_LIB
    NAMES libx264 x264
    PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
    NO_DEFAULT_PATH)

if (NOT X264_INCLUDE_DIRS OR NOT X264_LIB)
    message(FATAL_ERROR "Could not find x264 library")
else()
    message(STATUS "Found x264: ${X264_INCLUDE_DIRS} ${X264_LIB}")
endif()

set(X264_INCLUDE_DIRS "${X264_INCLUDE_DIRS}" CACHE PATH "Path to x264 include directory")
set(X264_LIB "${X264_LIB}" CACHE PATH "Path to x264 libs")
mark_as_advanced(X264_INCLUDE_DIRS X264_LIB)
