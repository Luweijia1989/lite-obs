# - Try to find LiteOBS SDK and Abi
#
# LiteOBS_FOUND - system has LiteOBS
# LiteOBS_INCLUDE_DIRS - the LiteOBS include directory
# LiteOBS_LIBRARIES - The LiteOBS libraries
# LiteOBS_VERSION_STRING -the version of LiteOBS SDK found
#
# target_link_libraries(tgt PRIVATE lite-obs) will add all flags

if(POLICY CMP0063) # visibility. since 3.3
  cmake_policy(SET CMP0063 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)
endif()

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
  set(CMAKE_SHARED_MODULE_SUFFIX ".dylib") # default is so
endif()


# Compute the installation prefix relative to this file.
get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
if(_IMPORT_PREFIX STREQUAL "/")
  set(_IMPORT_PREFIX "")
endif()

if(ANDROID_ABI)
  set(_IMPORT_ARCH ${ANDROID_ABI})
elseif(CMAKE_ANDROID_ARCH_ABI)
  set(_IMPORT_ARCH ${CMAKE_ANDROID_ARCH_ABI})
elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID) # msvc
  set(_IMPORT_ARCH ${CMAKE_C_COMPILER_ARCHITECTURE_ID}) # ARMV7 ARM64 X86 x64
elseif(WIN32)
  set(_IMPORT_ARCH ${CMAKE_SYSTEM_PROCESSOR})
elseif(CMAKE_SYSTEM_NAME STREQUAL Linux)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "[aA].*[rR].*64")
    set(_IMPORT_ARCH arm64)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "[aA][rR][mM]")
    set(_IMPORT_ARCH armhf)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "64")
    set(_IMPORT_ARCH amd64)
  endif()
endif()
string(TOLOWER "${_IMPORT_ARCH}" _IMPORT_ARCH)
if(WIN32)
  if(_IMPORT_ARCH MATCHES armv7) #msvc
    set(_IMPORT_ARCH arm)
  elseif(_IMPORT_ARCH MATCHES amd64) #msvc
    set(_IMPORT_ARCH x64)
  endif()
endif()

#list(APPEND CMAKE_FIND_ROOT_PATH ${_IMPORT_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH) # for cross build, find paths out sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH) # for cross build, find paths out sysroot

find_path(LiteOBS_INCLUDE_DIR lite-obs/lite_obs_global.h PATHS ${_IMPORT_PREFIX}/include)

find_library(LiteOBS_LIBRARY NAMES lite-obs liblite-obs PATHS ${_IMPORT_PREFIX}/lib/${_IMPORT_ARCH}) # FIXME: may select host library
if(LiteOBS_LIBRARY)
#TODO APPLE fix
  # if(APPLE)
  #   set(LiteOBS_LIBRARY ${LiteOBS_LIBRARY}/mdk) # was .framework, IMPORTED_LOCATION is file path
  # endif()
else()
  # if(APPLE)
  #   set(LiteOBS_XCFWK ${_IMPORT_PREFIX}/lib/mdk.xcframework)
  #   if(EXISTS ${LiteOBS_XCFWK})
  #     if(IOS)
  #       if(${CMAKE_OSX_SYSROOT} MATCHES Simulator)
  #         file(GLOB LiteOBS_FWK LIST_DIRECTORIES true ${LiteOBS_XCFWK}/ios-*-simulator)
  #       else()
  #         file(GLOB LiteOBS_FWK LIST_DIRECTORIES true ${LiteOBS_XCFWK}/ios-arm*)
  #       endif()
  #     else()
  #       file(GLOB LiteOBS_FWK LIST_DIRECTORIES true ${LiteOBS_XCFWK}/macos-*)
  #     endif()
  #     if(EXISTS ${LiteOBS_FWK})
  #       set(LiteOBS_LIBRARY ${LiteOBS_FWK}/mdk.framework/mdk)
  #     endif()
  #   endif()
  # endif()
endif()


set(LiteOBS_INCLUDE_DIRS ${LiteOBS_INCLUDE_DIR})
set(LiteOBS_ABI_INCLUDE_DIRS ${LiteOBS_ABI_INCLUDE_DIR})
set(LiteOBS_LIBRARIES ${LiteOBS_LIBRARY})
mark_as_advanced(LiteOBS_INCLUDE_DIRS LiteOBS_ABI_INCLUDE_DIRS LiteOBS_LIBRARIES)

if(LiteOBS_INCLUDE_DIR AND EXISTS "${LiteOBS_INCLUDE_DIR}/lite-obs/lite_obs_global.h")
  file(STRINGS "${LiteOBS_INCLUDE_DIR}/lite-obs/lite_obs_global.h" lite_obs_version_str
       REGEX "^#[\t ]*define[\t ]+LiteOBS_(MAJOR|MINOR|MICRO)[\t ]+[0-9]+$")

  unset(LiteOBS_VERSION_STRING)
  foreach(VPART MAJOR MINOR MICRO)
    foreach(VLINE ${lite_obs_version_str})
      if(VLINE MATCHES "^#[\t ]*define[\t ]+LiteOBS_${VPART}[\t ]+([0-9]+)$")
        set(LiteOBS_VERSION_PART "${CMAKE_MATCH_1}")
        if(DEFINED LiteOBS_VERSION_STRING)
          string(APPEND LiteOBS_VERSION_STRING ".${LiteOBS_VERSION_PART}")
        else()
          set(LiteOBS_VERSION_STRING "${LiteOBS_VERSION_PART}")
        endif()
        unset(LiteOBS_VERSION_PART)
      endif()
    endforeach()
  endforeach()
endif()

include(FindPackageHandleStandardArgs)

if(MSVC)
  set(CXX_FLAG_NO_RTTI -GR-)
else()
  set(CXX_FLAG_NO_RTTI -fno-rtti)
endif()

find_package_handle_standard_args(LiteOBS
                                  REQUIRED_VARS LiteOBS_LIBRARY LiteOBS_INCLUDE_DIR
                                  VERSION_VAR LiteOBS_VERSION_STRING)
add_library(lite-obs SHARED IMPORTED) # FIXME: ios needs CMAKE_SYSTEM_VERSION=9.0+(not DCMAKE_OSX_DEPLOYMENT_TARGET): Attempting to use @rpath without CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG being set.  This could be because you are using a Mac OS X version less than 10.5 or because CMake's platform configuration is corrupt
set_target_properties(lite-obs PROPERTIES
  IMPORTED_LOCATION "${LiteOBS_LIBRARIES}"
  IMPORTED_IMPLIB "${LiteOBS_LIBRARY}" # for win32, .lib import library
  INTERFACE_INCLUDE_DIRECTORIES "${LiteOBS_INCLUDE_DIRS}"
  #IMPORTED_SONAME "@rpath/mdk.framework/mdk"
  #IMPORTED_NO_SONAME 1 # -lmdk instead of full path
  )

if(APPLE)
  set_property(TARGET lite-obs PROPERTY FRAMEWORK 1)
else()
  if(ANDROID)
    add_library(lite-obs-ffmpeg SHARED IMPORTED)
    set_target_properties(lite-obs-ffmpeg PROPERTIES
            IMPORTED_LOCATION ${_IMPORT_PREFIX}/lib/${_IMPORT_ARCH}/libffmpeg.so
            )
    #add_dependencies(mdk mdk-ffmpeg)
    target_link_libraries(lite-obs INTERFACE lite-obs-ffmpeg) # ensure libffmpeg.so will be copied by gradle
  endif()
endif()
