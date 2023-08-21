set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
set(TARGET_OS macOS)

find_package(OpenGL REQUIRED)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC GL_SILENCE_DEPRECATION
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    OpenGL::GL
    "-framework AppKit"
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
