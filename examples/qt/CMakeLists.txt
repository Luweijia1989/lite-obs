cmake_minimum_required(VERSION 3.14)

project(liteobs-qt-example VERSION 0.1 LANGUAGES CXX)

#set(LITEOBS_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../output/android)

#if (LITEOBS_SDK_DIR)
#        # LITEOBS_SDK_DIR path set by user
#else()
#        set(LITEOBS_SDK_DIR "" CACHE PATH "Path to LITEOBS_SDK_DIR")
#        message(WARNING "LITEOBS_SDK_DIR variable is missing.  Please set this variable to specify path to LITEOBS_SDK_DIR")
#endif()

#include(${LITEOBS_SDK_DIR}/lib/cmake/FindLiteOBS.cmake)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick)

set(PROJECT_HEADERS
    lite-obs-example.h
    logorenderer.h
    fboinsgrenderer.h
)

set(PROJECT_SOURCES
    lite-obs-example.cpp
    logorenderer.cpp
    fboinsgrenderer.cpp
    main.cpp
    qml.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(${PROJECT_NAME}
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET liteobs-qt-example APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(${PROJECT_NAME} SHARED
            ${PROJECT_SOURCES}
            ${PROJECT_HEADERS}
        )
    # Define properties for Android with Qt 5 after find_package() calls as:
    #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
else()
    add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
    )
endif()
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick)

target_link_libraries(${PROJECT_NAME} PRIVATE lite-obs)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER liteobs.luweijia.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks")

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(${PROJECT_NAME})
    qt_finalize_executable(${PROJECT_NAME})
endif()

if (TARGET_OS STREQUAL iOS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/$<CONFIG>-iphoneos ${PROJECT_BINARY_DIR}/$<CONFIG>-iphoneos/${PROJECT_NAME}.app/Frameworks
    )
elseif (TARGET_OS STREQUAL windows-desktop)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${FFmpeg_BIN_FILE} ${PROJECT_BINARY_DIR}
    )
elseif (TARGET_OS STREQUAL android)
    add_library(lite-obs-ffmpeg SHARED IMPORTED)
    set_target_properties(lite-obs-ffmpeg PROPERTIES IMPORTED_LOCATION ${FFmpeg_LIBS})
    target_link_libraries(${PROJECT_NAME} INTERFACE lite-obs-ffmpeg) # ensure libffmpeg.so will be copied by gradle
endif()

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
