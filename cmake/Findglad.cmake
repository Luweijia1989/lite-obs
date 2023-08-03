find_path(glad_INCLUDE_DIRS NAMES glad/glad.h PATHS ${DepsPath} PATH_SUFFIXES include)

find_library(
    glad_LIBS
    NAMES glad
    PATHS ${DepsPath}/${SUFFIX}/lib)


if (NOT glad_INCLUDE_DIRS OR NOT glad_LIBS)
    message(FATAL_ERROR "Could not find glad library")
else()
    message(STATUS "Found glad: ${glad_INCLUDE_DIRS} ${glad_LIBS}")
endif()

# Export GLM include directories
set(glad_INCLUDE_DIRS "${glad_INCLUDE_DIRS}" CACHE PATH "Path to glad include directory")
set(glad_LIBS "${glad_LIBS}" CACHE PATH "Path to glad libs")
mark_as_advanced(glad_INCLUDE_DIRS glad_LIBS)
