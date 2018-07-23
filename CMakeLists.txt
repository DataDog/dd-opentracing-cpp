cmake_minimum_required(VERSION 3.1)

project(dd-opentracing-cpp)

option(BUILD_SHARED "Builds shared library" ON)
option(BUILD_STATIC "Builds static library" OFF)
option(BUILD_PLUGIN "Builds plugin (requires gcc and not macos)" OFF)
option(BUILD_TESTING "Builds tests, also enables BUILD_SHARED" OFF)
option(HEADERS_ONLY "Only generate version_number.h" OFF)

if(BUILD_TESTING)
  set(BUILD_SHARED ON)
endif()

# Configure the compiler.
set(CMAKE_CXX_STANDARD 14)

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

configure_file(
  "${PROJECT_SOURCE_DIR}/src/version_number.h.in"
  "${PROJECT_BINARY_DIR}/version_number.h")
include_directories("${PROJECT_BINARY_DIR}")

if(HEADERS_ONLY)
  return()
endif()

# Dependencies
find_path(OPENTRACING_INCLUDE_DIR NAMES opentracing/tracer.h)
find_library(OPENTRACING_LIB opentracing)
find_library(MSGPACK_LIB msgpack)
find_package(CURL)
find_package(Threads REQUIRED)
# Code Sanitizers, for testing.
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rd_party/sanitizers-cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers)

set(DATADOG_LINK_LIBRARIES ${OPENTRACING_LIB} ${CURL_LIBRARIES} Threads::Threads)

# Includes
include_directories(SYSTEM 3rd_party/include)
include_directories(SYSTEM ${OPENTRACING_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})
include_directories(include)

# Code
install(DIRECTORY include/datadog DESTINATION include)
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")

# Outputs
## Shared lib
if(BUILD_SHARED)
  add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing)
  target_link_libraries(dd_opentracing ${DATADOG_LINK_LIBRARIES})

  install(TARGETS dd_opentracing
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif()

## Static lib
if(BUILD_STATIC)
  add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-static)
  set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing)
  target_link_libraries(dd_opentracing-static ${DATADOG_LINK_LIBRARIES} datadog)

  install(TARGETS dd_opentracing-static
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif()

## Plugin
if(BUILD_PLUGIN)
  if(CMAKE_SYSTEM_NAME MATCHES "Darwin" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Plugin cannot be built on Mac, and requires GCC")
  endif()

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/export.map
      "{ global: OpenTracingMakeTracerFactory; local: *; };")
  add_library(dd_opentracing_plugin MODULE ${DD_OPENTRACING_SOURCES})
  target_link_libraries(dd_opentracing_plugin PUBLIC ${DATADOG_LINK_LIBRARIES}
                        -static-libstdc++
                        -static-libgcc
                        -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/export.map)
  install(TARGETS dd_opentracing_plugin
          LIBRARY DESTINATION lib)
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
