cmake_minimum_required(VERSION 3.14)

project(dd-opentracing-cpp CXX C)

set(SOVERSION 0)

# Introduce variables:
# * CMAKE_INSTALL_LIBDIR
# * CMAKE_INSTALL_BINDIR
# * CMAKE_INSTALL_INCLUDEDIR
include(GNUInstallDirs)

option(BUILD_SHARED "Builds shared library" ON)
option(BUILD_STATIC "Builds static library" OFF)
option(BUILD_OBJECT "Builds objects for use in another project" OFF)
option(BUILD_PLUGIN "Builds plugin (requires gcc and not macos)" OFF)
option(BUILD_TESTING "Builds tests, also enables BUILD_SHARED" OFF)
option(BUILD_COVERAGE "Builds code with code coverage profiling instrumentation" OFF)

if(BUILD_TESTING)
  set(BUILD_SHARED ON)
endif()

# Configure the compiler.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 14)

include(Dependencies.cmake)
add_external_dependencies()

# Dependencies
find_package(Threads REQUIRED)

# Code Sanitizers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/sanitizers-cmake")
find_package(Sanitizers)

# Code
install(DIRECTORY include/datadog DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")
message(STATUS "Compiler ID: ${CMAKE_CXX_COMPILER_ID}")

add_library(datadog_project_options INTERFACE)

if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  target_compile_options(datadog_project_options INTERFACE -Wall -Wextra -pedantic)
  if(BUILD_COVERAGE)
      target_compile_options(datadog_project_options INTERFACE -g -O0 -fprofile-arcs -ftest-coverage)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(datadog_project_options INTERFACE /W3)
else()
  message(FATAL_ERROR "Unknown compiler ${CMAKE_CXX_COMPILER_ID}")
endif()

# Outputs
if(BUILD_COVERAGE)
  set(COVERAGE_LIBRARIES gcov)
endif()
set(DATADOG_LINK_LIBRARIES  
    msgpack-cxx
    libcurl Threads::Threads nlohmann_json::nlohmann_json Catch2::Catch2WithMain CLI11::CLI11 ${COVERAGE_LIBRARIES} datadog_project_options opentelemetry_api)

## Shared lib
if(BUILD_SHARED)
  add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing)
  if(BUILD_COVERAGE)
    target_link_options(dd_opentracing PRIVATE -fprofile-arcs -ftest-coverage)
  endif()
  target_link_libraries(dd_opentracing opentelemetry_opentracing_shim ${DATADOG_LINK_LIBRARIES})
  set_target_properties(dd_opentracing PROPERTIES SOVERSION ${SOVERSION})
  target_include_directories(dd_opentracing PUBLIC include)
  target_compile_definitions(dd_opentracing PUBLIC DD_OPENTRACING_SHARED)
  install(TARGETS dd_opentracing
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

## Static lib
if(BUILD_STATIC)
  add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-static)
  target_link_libraries(dd_opentracing-static opentelemetry_opentracing_shim_static ${DATADOG_LINK_LIBRARIES})
  set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing POSITION_INDEPENDENT_CODE ON)
  target_include_directories(dd_opentracing-static PUBLIC include)
  target_compile_definitions(dd_opentracing-static PUBLIC DD_OPENTRACING_STATIC)
  install(TARGETS dd_opentracing-static
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

## Object lib
if(BUILD_OBJECT)
  add_library(dd_opentracing-object OBJECT ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-object)
  target_link_libraries(dd_opentracing-object opentelemetry_opentracing_shim ${DATADOG_LINK_LIBRARIES} ${CURL_LIBRARIES} Threads::Threads)
  target_include_directories(dd_opentracing-object PUBLIC include)
  set_property(TARGET dd_opentracing-object PROPERTY POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(dd_opentracing-object PUBLIC DD_OPENTRACING_OBJECT)
endif()

## Plugin
if(BUILD_PLUGIN)
  if(CMAKE_SYSTEM_NAME MATCHES "Darwin" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Plugin cannot be built on Mac, and requires GCC")
  endif()

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/export.map
      "{ global: OpenTracingMakeTracerFactory; local: *; };")
  add_library(dd_opentracing_plugin MODULE ${DD_OPENTRACING_SOURCES})
  if(BUILD_COVERAGE)
    target_link_options(dd_opentracing_plugin PRIVATE -fprofile-arcs -ftest-coverage)
  endif()
  target_link_libraries(dd_opentracing_plugin PUBLIC opentelemetry_opentracing_shim ${DATADOG_LINK_LIBRARIES}
                        -static-libstdc++
                        -static-libgcc
                        -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/export.map)
  target_include_directories(dd_opentracing_plugin PUBLIC include)
  install(TARGETS dd_opentracing_plugin
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
