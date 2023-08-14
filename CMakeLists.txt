##################################################    Project     ##################################################
cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project               (lite-obs VERSION 1.0 LANGUAGES CXX C)
list                  (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set_property          (GLOBAL PROPERTY USE_FOLDERS ON)

option(LITEOBS_WITH_SANITIZER "Use [address|thread|undefined] here" OFF)
option(LITEOBS_WITH_RTTI "Compile with runtime type information" OFF)
option(LITEOBS_WITH_WERROR "Make all compilation warnings errors" ON)

add_library(
    liteobs-compiler-options INTERFACE
)

set(UBSAN_BLACKLIST "")
target_compile_options(
    liteobs-compiler-options
    INTERFACE
    $<$<BOOL:${LITEOBS_WITH_SANITIZER}>:-DSANITIZE>
    $<$<BOOL:${LITEOBS_WITH_SANITIZER}>:-fno-omit-frame-pointer>
    $<$<BOOL:${LITEOBS_WITH_SANITIZER}>:-fno-optimize-sibling-calls>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},address>:-fsanitize=address>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},thread>:-fsanitize=thread>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=implicit-conversion,
    -fsanitize-blacklist=${UBSAN_BLACKLIST}>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=undefined,
    -fsanitize-blacklist=${UBSAN_BLACKLIST}>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=unsigned-integer-overflow,
    -fsanitize-blacklist=${UBSAN_BLACKLIST}>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=nullability,
    -fsanitize-blacklist=${UBSAN_BLACKLIST}>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=float-divide-by-zero,
    -fsanitize-blacklist=${UBSAN_BLACKLIST}>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<BOOL:${LITEOBS_WITH_RTTI}>>,$<NOT:$<PLATFORM_ID:Windows>>>:-fno-rtti>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wall>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wshadow>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wextra>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<PLATFORM_ID:Windows>>,$<BOOL:${LITEOBS_WITH_WERROR}>>:-Werror>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<PLATFORM_ID:Android>>:-Wno-error=tautological-constant-compare>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-error=maybe-uninitialized>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-error=return-type>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-error=unknown-pragmas>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-error=deprecated-volatile>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-volatile>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-error=deprecated-declarations>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-error=unused-parameter>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-error=unused-property-ivar>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-error=deprecated-volatile>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-unused-parameter>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-deprecated-declarations>
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-deprecated-volatile>
)

target_link_libraries(
    liteobs-compiler-options
    INTERFACE
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},address>:-fsanitize=address>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},thread>:-fsanitize=thread>
    $<$<STREQUAL:${LITEOBS_WITH_SANITIZER},undefined>:-fsanitize=undefined>
)

cmake_policy(SET CMP0063 NEW)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

if(ANDROID)
    set(DepsPath "D:/vcpkg/installed/arm-android" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
    set(FFmpegPath "D:/ffmpeg-6.0-android-lite" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
elseif(APPLE)
    if(IOS)
        set(DepsPath "/Users/luweijia/vcpkg/installed/arm64-ios" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
        set(FFmpegPath "/Users/luweijia/Downloads/ffmpeg-6.0-iOS-gpl-lite" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
    else()
        set(DepsPath "/Users/luweijia/vcpkg/installed/x64-osx" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
        set(FFmpegPath "/Users/luweijia/Downloads/ffmpeg-6.0-macOS-gpl-lite" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
    endif()
else()
    set(DepsPath "D:/vcpkg/installed/x64-windows-static-md" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
    set(FFmpegPath "D:/ffmpeg-master-windows-desktop-vs2022-lite" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
endif()

if (DepsPath OR DEFINED ENV{DepsPath})
    # Dependencies path set by user or env var
else()
    set(DepsPath "" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
    message(WARNING "DepsPath variable is missing.  Please set this variable to specify path to compiled dependencies (e.g. D:/dependencies/win64)")
endif()

if (FFmpegPath OR DEFINED ENV{FFmpegPath})
    # FFmpeg path set by user or env var
else()
    set(FFmpegPath "" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
    message(WARNING "FFmpeg variable is missing.  Please set this variable to specify path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
endif()

##################################################    Options     ##################################################
option(BUILD_EXAMPLES "Build examples." ON)

##################################################    Sources     ##################################################
unset(PROJECT_SOURCES CACHE)
file(GLOB_RECURSE PROJECT_HEADERS include/*.h include/*.hpp include/*.ipp)
file(GLOB_RECURSE PROJECT_SOURCES source/*.cpp source/*.mm source/*.c source/*.h)
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/source/third-party/.*$")
file(GLOB_RECURSE PROJECT_CMAKE_UTILS cmake/*.cmake)
file(GLOB_RECURSE PROJECT_MISC *.bat *.gitignore *.md *.py *.sh *.txt)

set (PROJECT_EXPORT_HPP include/${PROJECT_NAME}/lite_obs_export.hpp)
set (PROJECT_FILES
    ${PROJECT_HEADERS}
    ${PROJECT_SOURCES}
    ${PROJECT_PLATFORM_HEADERS}
    ${PROJECT_PLATFORM_SOURCES}
    ${PROJECT_CMAKE_UTILS}
    ${PROJECT_MISC}
    ${PROJECT_EXPORT_HPP})

include            (assign_source_group)
assign_source_group(${PROJECT_FILES})

##################################################  Dependencies  ##################################################
include(import_library)

if(ANDROID OR APPLE)
    #library search issue. https://stackoverflow.com/questions/46055431/cmake-on-android-find-library-fail-to-find-with-custom-path
    #for cross compile
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(SUFFIX "debug")
endif()

find_package(FFmpeg REQUIRED)
import_library(FFmpeg_INCLUDE_DIRS FFmpeg_LIBS)

find_package(SRT REQUIRED)
import_library(SRT_INCLUDE_DIRS SRT_LIBS)

##################################################    Targets     ##################################################
add_library(${PROJECT_NAME} SHARED ${PROJECT_FILES})

#define NO_CRYPTO to disable rtmps. openssl exists because srt depends on it, so define HAVE_OPENSSL
target_compile_definitions(${PROJECT_NAME} PUBLIC NO_CRYPTO HAVE_OPENSSL)

if(CMAKE_SYSTEM_NAME STREQUAL Android)
    include(${PROJECT_SOURCE_DIR}/cmake/platform/android.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL iOS)
    include(${PROJECT_SOURCE_DIR}/cmake/platform/ios.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL Windows)
    include(${PROJECT_SOURCE_DIR}/cmake/platform/windows.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    include(${PROJECT_SOURCE_DIR}/cmake/platform/macos.cmake)
else()
    message(FATAL_ERROR "Unsupported target platform: " ${CMAKE_SYSTEM_NAME})
endif()

add_subdirectory(source/third-party/glm)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include> PRIVATE source)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries     (${PROJECT_NAME} PUBLIC ${PROJECT_LIBRARIES} liteobs-compiler-options glm)

##################################################    Postbuild   ##################################################
include               (GenerateExportHeader)
string                (TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)
generate_export_header(${PROJECT_NAME} 
    EXPORT_FILE_NAME     ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/lite_obs_export.hpp
    EXPORT_MACRO_NAME    ${PROJECT_NAME_UPPER}_EXPORT
    STATIC_DEFINE        ${PROJECT_NAME_UPPER}_STATIC
)

##################################################    Examples    ##################################################
if(BUILD_EXAMPLES)
    add_subdirectory(examples/qt)
endif()
