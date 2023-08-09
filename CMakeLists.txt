##################################################    Project     ##################################################
cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
#add ANDROID_PLATFORM will cause windows configure fail
#set                   (ANDROID_PLATFORM "android-21")
project               (lite-obs VERSION 1.0)
list                  (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set_property          (GLOBAL PROPERTY USE_FOLDERS ON)
set                   (CMAKE_CXX_STANDARD 20)
set                   (CMAKE_CXX_VISIBILITY_PRESET hidden)
set                   (CMAKE_VISIBILITY_INLINES_HIDDEN 1)

if(ANDROID)
    #library search issue. https://stackoverflow.com/questions/46055431/cmake-on-android-find-library-fail-to-find-with-custom-path
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
elseif(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
endif()

if(ANDROID)
    set(DepsPath "D:/vcpkg/installed/arm-android" CACHE PATH "Path to compiled dependencies (e.g. D:/dependencies/win64)")
    set(FFmpegPath "D:/ffmpeg-6.0-android-lite" CACHE PATH "Path to compiled ffmpeg (e.g. D:/ffmpeg/windows)")
elseif(APPLE)
    if(IOS)
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

if   (MSVC)
    add_compile_options (/W3)
else ()
    add_compile_options (-Wall -Wextra -pedantic)
endif()

##################################################    Options     ##################################################
option(BUILD_SHARED_LIBS "Build shared (dynamic) libraries." ON)
option(BUILD_EXAMPLES "Build examples." ON)

##################################################    Sources     ##################################################
unset(PROJECT_SOURCES CACHE)
file(GLOB_RECURSE PROJECT_HEADERS include/*.h include/*.hpp include/*.ipp)
file(GLOB_RECURSE PROJECT_SOURCES source/*.cpp source/*.mm source/*.c source/*.h)
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/source/platform-dep/.*$")
file(GLOB_RECURSE PROJECT_CMAKE_UTILS cmake/*.cmake)
file(GLOB_RECURSE PROJECT_MISC *.bat *.gitignore *.md *.py *.sh *.txt)

set (PROJECT_EXPORT_HPP include/${PROJECT_NAME}/export.hpp)
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

if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(SUFFIX "debug")
endif()

find_package(GLM REQUIRED)
import_library(GLM_INCLUDE_DIRS)

find_package(FFmpeg REQUIRED)
import_library(FFmpeg_INCLUDE_DIRS FFmpeg_LIBS)

find_package(SRT REQUIRED)
import_library(SRT_INCLUDE_DIRS SRT_LIBS)

# ADD LIBRARIES HERE.
# Examples:
# - Header Only:
#   find_package  (GLM REQUIRED)
#   import_library(GLM_INCLUDE_DIRS)
# - Identical Debug and Release:
#   find_package  (OpenGL REQUIRED)
#   import_library(OPENGL_INCLUDE_DIR OPENGL_LIBRARIES)
# - Separate Debug and Release:
#   find_package  (Boost REQUIRED iostreams)
#   import_library(Boost_INCLUDE_DIRS Boost_IOSTREAMS_LIBRARY_DEBUG Boost_IOSTREAMS_LIBRARY_RELEASE)
# - You may also set the PROJECT_INCLUDE_DIRS and PROJECT_LIBRARIES instead of using import_library.

##################################################    Targets     ##################################################
add_library(${PROJECT_NAME} ${PROJECT_FILES})

#define NO_CRYPTO to disable rtmps. openssl exists because srt depends on it, so define HAVE_OPENSSL
target_compile_definitions(${PROJECT_NAME} PUBLIC NO_CRYPTO HAVE_OPENSSL)

if(WIN32)
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /wd4996")
    add_subdirectory(source/platform-dep/windows/glad ${CMAKE_BINARY_DIR}/libs)
    target_link_libraries(${PROJECT_NAME} PRIVATE glad)
    target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 Winmm Crypt32)
elseif(ANDROID)
    if(${ANDROID_ABI} STREQUAL "armeabi-v7a")
        set(ARCH "arm-linux-androideabi")
    endif()

    string(REPLACE "-" ";" output_list ${ANDROID_PLATFORM})
    list(GET output_list 1 ANDROID_API_LEVEL)

    find_path(GLES3_INCLUDE_DIR GLES3/gl3.h
        PATHS ${CMAKE_SYSROOT}
        PATH_SUFFIXES usr/include)

    find_library(GLES3_LIBRARY libGLESv3.so
        HINTS ${GLES3_INCLUDE_DIR}/../lib/${ARCH}/${ANDROID_API_LEVEL})

    find_library(EGL_LIBRARY libEGL.so
        HINTS ${GLES3_INCLUDE_DIR}/../lib/${ARCH}/${ANDROID_API_LEVEL})

    target_link_libraries(lite-obs PRIVATE ${GLES3_LIBRARY} ${EGL_LIBRARY})
elseif(APPLE)
    find_library(OPENGL OpenGL)
    find_library(APPKIT AppKit)
    target_link_libraries(lite-obs PRIVATE ${OPENGL} ${APPKIT})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include> PRIVATE source)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries     (${PROJECT_NAME} PUBLIC ${PROJECT_LIBRARIES})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PROJECT_COMPILE_DEFINITIONS})
target_compile_options    (${PROJECT_NAME} PUBLIC ${PROJECT_COMPILE_OPTIONS})
set_target_properties     (${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

if(NOT BUILD_SHARED_LIBS)
    string               (TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)
    set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS -D${PROJECT_NAME_UPPER}_STATIC)
endif()

##################################################    Postbuild   ##################################################
include               (GenerateExportHeader)
string                (TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)
generate_export_header(${PROJECT_NAME} 
    EXPORT_FILE_NAME     ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/export.hpp
    EXPORT_MACRO_NAME    ${PROJECT_NAME_UPPER}_EXPORT
    STATIC_DEFINE        ${PROJECT_NAME_UPPER}_STATIC
)

##################################################    Testing     ##################################################
if(BUILD_EXAMPLES)
    add_subdirectory(examples/qt)
endif()

##################################################  Installation  ##################################################
install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}-config
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT  ${PROJECT_NAME}-config DESTINATION cmake)
export (TARGETS ${PROJECT_NAME}        FILE        ${PROJECT_NAME}-config.cmake)
