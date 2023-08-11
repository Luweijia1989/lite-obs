set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11")

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)

find_package(OpenGL REQUIRED)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC GL_SILENCE_DEPRECATION
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE OpenGL::GL
)

target_compile_options(${PROJECT_NAME} PRIVATE -fobjc-arc)

target_link_libraries(
    mbgl-core
    PRIVATE
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework CoreLocation"
    "-framework SystemConfiguration"
)
