find_path(SRT_INCLUDE_DIRS NAMES srt/srt.h
    PATHS ${DepsPath}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH)

find_library(
    SRT_LIB
    NAMES srt
    PATHS ${DepsPath}/${SUFFIX}/lib
    NO_DEFAULT_PATH)

if(MSVC)
    find_library(
        SSL_LIB
        NAMES libssl
        PATHS ${DepsPath}/${SUFFIX}/lib
        NO_DEFAULT_PATH)

    find_library(
        CRYPTO_LIB
        NAMES libcrypto
        PATHS ${DepsPath}/${SUFFIX}/lib
        NO_DEFAULT_PATH)
else()
    find_library(
        SSL_LIB
        NAMES ssl
        PATHS ${DepsPath}/${SUFFIX}/lib
        NO_DEFAULT_PATH)

    find_library(
        CRYPTO_LIB
        NAMES crypto
        PATHS ${DepsPath}/${SUFFIX}/lib
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
