cmake_minimum_required(VERSION 3.13)

project(dd-opentracing-cpp)

set(SOVERSION 0)

# Introduce variables:
# * CMAKE_INSTALL_LIBDIR
# * CMAKE_INSTALL_BINDIR
# * CMAKE_INSTALL_INCLUDEDIR
include(GNUInstallDirs)

option(BUILD_SHARED "Builds shared library" OFF)
option(BUILD_STATIC "Builds static library" ON)
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
set(CMAKE_CXX_STANDARD 20)

# Includes
include_directories(SYSTEM 3rd_party/include deps/include)
include_directories(include)

# Libraries
set(CMAKE_LIBRARY_PATH deps/lib)

# Dependencies
include(cmake/CPM.cmake)
set(CPM_USE_LOCAL_PACKAGES ON)
CPMAddPackage("gh:catchorg/Catch2@3.3.2")
CPMAddPackage("gh:CLIUtils/CLI11@2.3.2")
CPMAddPackage(
  NAME opentelemetry-cpp
  GITHUB_REPOSITORY maztheman/opentelemetry-cpp
  GIT_TAG main
  OPTIONS
  "WITH_STL ON" "WITH_OPENTRACING ON" "BUILD_TESTING OFF" "BUILD_STATIC_LIBS ON"
)
CPMAddPackage("gh:curl/curl#curl-8_2_1")
CPMAddPackage(
  NAME msgpack-c 
  GITHUB_REPOSITORY msgpack/msgpack-c
  GIT_TAG cpp-6.1.0
  OPTIONS "MSGPACK_CXX20 ON"
)
CPMAddPackage(
  NAME nlohmann_json
  VERSION 3.11.2
  GITHUB_REPOSITORY nlohmann/json
  GITHUB_TAG v3.11.2
  OPTIONS
    "JSON_BuildTests OFF"
)
find_package(Threads REQUIRED)

# Code Sanitizers
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/sanitizers-cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers)

# Code
install(DIRECTORY include/datadog DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")
message(STATUS "Compiler ID: ${CMAKE_CXX_COMPILER_ID}")
if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  add_compile_options(-Wall -Wextra -pedantic)
  if(BUILD_COVERAGE)
      add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/W3)
else()
  message(FATAL_ERROR "Unknown compiler ${CMAKE_CXX_COMPILER_ID}")
endif()

# Outputs
if(BUILD_COVERAGE)
  set(COVERAGE_LIBRARIES gcov)
endif()
set(DATADOG_LINK_LIBRARIES opentelemetry_opentracing_shim 
    
    msgpack-cxx
    libcurl Threads::Threads nlohmann_json::nlohmann_json Catch2::Catch2WithMain CLI11::CLI11 ${COVERAGE_LIBRARIES})

## Shared lib
if(BUILD_SHARED)
  add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing)
  if(BUILD_COVERAGE)
    target_link_options(dd_opentracing PRIVATE -fprofile-arcs -ftest-coverage)
  endif()
  target_link_libraries(dd_opentracing ${DATADOG_LINK_LIBRARIES})
  set_target_properties(dd_opentracing PROPERTIES SOVERSION ${SOVERSION})
  target_compile_definitions(dd_opentracing PUBLIC DD_OPENTRACING_SHARED)
  install(TARGETS dd_opentracing
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

## Static lib
if(BUILD_STATIC)
  add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-static)
  target_link_libraries(dd_opentracing-static ${DATADOG_LINK_LIBRARIES})
  set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(dd_opentracing-static PUBLIC DD_OPENTRACING_STATIC)
  install(TARGETS dd_opentracing-static
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

## Object lib
if(BUILD_OBJECT)
  add_library(dd_opentracing-object OBJECT ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-object)
  target_link_libraries(dd_opentracing-object ${CURL_LIBRARIES} Threads::Threads)
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
  target_link_libraries(dd_opentracing_plugin PUBLIC ${DATADOG_LINK_LIBRARIES}
                        -static-libstdc++
                        -static-libgcc
                        -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/export.map)
  install(TARGETS dd_opentracing_plugin
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
