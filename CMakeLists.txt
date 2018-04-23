cmake_minimum_required (VERSION 3.0)

project(dd-opentracing-cpp)

# Configure the compiler.
set(CMAKE_CXX_STANDARD 14)
# This not needed on Mac, but is needed on GNU/Linux.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# If a git tag is set then use that as the version number,
# otherwise use short commit hash. If git unavailable,
# "unknown" is used.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
  find_package(Git)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --always
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE "VERSION"
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Git version: ${VERSION}")
  else(GIT_FOUND)
    set(VERSION "unknown")
  endif(GIT_FOUND)
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/version_number.h.in"
  "${PROJECT_BINARY_DIR}/version_number.h")
include_directories("${PROJECT_BINARY_DIR}")

# Dependencies
find_path(OPENTRACING_INCLUDE_DIR NAMES opentracing/tracer.h)
find_library(OPENTRACING_LIB opentracing)
find_library(MSGPACK_LIB msgpack)
find_package(CURL)

set(DATADOG_LINK_LIBRARIES ${OPENTRACING_LIB} ${CURL_LIBRARIES})

# Includes
include_directories(SYSTEM 3rd_party/include)
include_directories(SYSTEM ${OPENTRACING_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})
include_directories(include)

# Code
install(DIRECTORY include/datadog DESTINATION include)
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")

# Outputs
## Shared lib
add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
target_link_libraries(dd_opentracing ${DATADOG_LINK_LIBRARIES})

## Static lib
add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing)
target_link_libraries(dd_opentracing-static ${DATADOG_LINK_LIBRARIES} datadog)

install(TARGETS dd_opentracing dd_opentracing-static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# Tests
include(CTest)
if (BUILD_TESTING)
  add_subdirectory(test)
endif()
