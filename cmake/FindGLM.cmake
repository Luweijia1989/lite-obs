find_path(GLM_INCLUDE_DIRS NAMES glm/glm.hpp PATHS ${DepsPath} PATH_SUFFIXES include)

if (NOT GLM_INCLUDE_DIRS)
    message(FATAL_ERROR "Could not find GLM library")
else()
    message(STATUS "Found GLM: ${GLM_INCLUDE_DIRS}")
endif()

# Export GLM include directories
set(GLM_INCLUDE_DIRS "${GLM_INCLUDE_DIRS}" CACHE PATH "Path to GLM include directory")
mark_as_advanced(GLM_INCLUDE_DIRS)
