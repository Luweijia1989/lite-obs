if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
    set(IOS_DEPLOYMENT_TARGET "9.0")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC LITEOBS_USE_GLES GLES_SILENCE_DEPRECATION
)

target_link_libraries(
    ${PROJECT_NAME}
    ${PROJECT_NAME} "-framework GLKit" "-framework OpenGLES"
)

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${IOS_DEPLOYMENT_TARGET}")
set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_ENABLE_BITCODE "YES")
set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_BITCODE_GENERATION_MODE bitcode)
set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH $<$<CONFIG:Debug>:YES>)

unset(IOS_DEPLOYMENT_TARGET CACHE)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    "-framework CoreGraphics"
    "-framework CoreImage"
    "-framework CoreLocation"
    "-framework CoreServices"
    "-framework CoreText"
    "-framework Foundation"
    "-framework ImageIO"
    "-framework QuartzCore"
    "-framework Security"
    "-framework SystemConfiguration"
    "-framework UIKit"
    "-framework WebKit"
)
