set(IOS_DEPLOYMENT_TARGET "9.0")

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC LITEOBS_USE_GLES GLES_SILENCE_DEPRECATION
)

set(BITCODE_VALUE "NO")
set(BITCODE_GENERATION_VALUE marker)

set(PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/lite-obs/lite_obs.h
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    FRAMEWORK TRUE
    MACOSX_FRAMEWORK_IDENTIFIER com.bixin.LiteObsSDK
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
    INSTALL_NAME_DIR @rpath
    XCODE_ATTRIBUTE_LD_DYLIB_INSTALL_NAME "@rpath/LiteObsSDK.framework/ListObsSDK"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
    #simulator disable arm64 architecture
    XCODE_ATTRIBUTE_EXCLUDED_ARCHS[sdk=iphonesimulator*] arm64
    XCODE_ATTRIBUTE_APPLECLANG_GENERATE_DEBUGGING_SYMBOLS YES
    XCODE_ATTRIBUTE_ENABLE_BITCODE ${BITCODE_VALUE}
    XCODE_ATTRIBUTE_DEFINES_MODULE "YES"
    XCODE_ATTRIBUTE_BITCODE_GENERATION_MODE ${BITCODE_GENERATION_VALUE}
    CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${IOS_DEPLOYMENT_TARGET}
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    "-framework GLKit"
    "-framework OpenGLES"
)



