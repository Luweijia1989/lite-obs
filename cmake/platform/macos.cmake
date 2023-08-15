set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
set(TARGET_OS macOS)

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)

find_package(OpenGL REQUIRED)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC GL_SILENCE_DEPRECATION
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE OpenGL::GL "-framework AppKit"
)

target_compile_options(${PROJECT_NAME} PRIVATE -fobjc-arc)
