set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /wd4996")
add_subdirectory(source/platform-dep/windows/glad ${CMAKE_BINARY_DIR}/libs)
target_link_libraries(${PROJECT_NAME} PRIVATE glad)
target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 Winmm Crypt32)