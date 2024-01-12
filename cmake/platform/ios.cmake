set(IOS_DEPLOYMENT_TARGET "8.0")
set(TARGET_OS iOS)

set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC LITEOBS_USE_GLES GLES_SILENCE_DEPRECATION
)

set(BITCODE_VALUE "NO")
set(BITCODE_GENERATION_VALUE marker)

set(PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/lite-obs/lite_obs_callback.h
    ${PROJECT_SOURCE_DIR}/include/lite-obs/lite_obs_defines.h
    ${PROJECT_SOURCE_DIR}/include/lite-obs/lite_obs_global.h
    ${PROJECT_SOURCE_DIR}/include/lite-obs/lite_obs.h
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    FRAMEWORK ON
    MACOSX_FRAMEWORK_IDENTIFIER com.bixin.LiteObsSDK
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
    XCODE_ATTRIBUTE_ENABLE_BITCODE ${BITCODE_VALUE}
    XCODE_ATTRIBUTE_BITCODE_GENERATION_MODE ${BITCODE_GENERATION_VALUE}
    CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${IOS_DEPLOYMENT_TARGET}
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    "-framework GLKit"
    "-framework OpenGLES"
    "-framework AudioToolBox"
    "-framework VideoToolBox"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework Security"
    bz2
    iconv
    libz.tbd
    xml2
)



