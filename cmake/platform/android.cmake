if(NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
    message(FATAL_ERROR "-- Toolchain file not included, see https://developer.android.com/ndk/guides/cmake")
endif()

set(TARGET_OS android)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC LITEOBS_USE_GLES
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/source/third-party/JMI)

# cmake-format: off
target_compile_options(liteobs-compiler-options INTERFACE $<$<CONFIG:Release>:-Oz> $<$<CONFIG:Release>:-Qunused-arguments> $<$<CONFIG:Release>:-flto>)
# cmake-format: on

target_link_libraries(
    liteobs-compiler-options
    INTERFACE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-Wl,--icf=all>
    $<$<CONFIG:Release>:-flto>
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    EGL
    GLESv3
    android
    z
    mediandk
    jmi
)

