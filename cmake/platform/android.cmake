if(NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
    message(FATAL_ERROR "-- Toolchain file not included, see https://developer.android.com/ndk/guides/cmake")
endif()

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC LITEOBS_USE_GLES
)

# cmake-format: off
target_compile_options(liteobs-compiler-options INTERFACE $<$<CONFIG:Release>:-Oz> $<$<CONFIG:Release>:-Qunused-arguments> $<$<CONFIG:Release>:-flto>)
# cmake-format: on

target_link_libraries(
    liteobs-compiler-options
    INTERFACE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-Wl,--icf=all>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Release>:-fuse-ld=gold>
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    EGL
    GLESv3
)
