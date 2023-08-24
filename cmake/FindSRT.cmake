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

find_path(SRT_INCLUDE_DIRS NAMES srt/srt.h
    PATHS ${THIRDPARTY_PATH}
    PATH_SUFFIXES ${ARCH}/include
    NO_DEFAULT_PATH)

find_library(
    SRT_LIB
    NAMES srt
    PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
    NO_DEFAULT_PATH)

if(MSVC)
    find_library(
        SSL_LIB
        NAMES libssl
        PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
        NO_DEFAULT_PATH)

    find_library(
        CRYPTO_LIB
        NAMES libcrypto
        PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
        NO_DEFAULT_PATH)
else()
    find_library(
        SSL_LIB
        NAMES ssl
        PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
        NO_DEFAULT_PATH)

    find_library(
        CRYPTO_LIB
        NAMES crypto
        PATHS ${THIRDPARTY_PATH}/${ARCH}/${SUFFIX}/lib
        NO_DEFAULT_PATH)
endif()

set(SRT_LIBS ${SRT_LIB} ${SSL_LIB} ${CRYPTO_LIB})

if (NOT SRT_INCLUDE_DIRS OR NOT SRT_LIBS)
    message(FATAL_ERROR "Could not find SRT library")
else()
    message(STATUS "Found SRT: ${SRT_INCLUDE_DIRS} ${SRT_LIBS}")
endif()

set(SRT_INCLUDE_DIRS "${SRT_INCLUDE_DIRS}" CACHE PATH "Path to SRT include directory")
set(SRT_LIBS "${SRT_LIBS}" CACHE PATH "Path to SRT libs")
mark_as_advanced(SRT_INCLUDE_DIRS SRT_LIBS)
