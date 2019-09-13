cmake_minimum_required(VERSION 3.1)

project(dd-opentracing-cpp)

set(SOVERSION 0)

# Introduce variables:
# * CMAKE_INSTALL_LIBDIR
# * CMAKE_INSTALL_BINDIR
# * CMAKE_INSTALL_INCLUDEDIR
include(GNUInstallDirs)

option(BUILD_SHARED "Builds shared library" ON)
option(BUILD_STATIC "Builds static library" OFF)
option(BUILD_PLUGIN "Builds plugin (requires gcc and not macos)" OFF)
option(BUILD_TESTING "Builds tests, also enables BUILD_SHARED" OFF)

if(BUILD_TESTING)
  set(BUILD_SHARED ON)
endif()

# Configure the compiler.
set(CMAKE_CXX_STANDARD 14)

# Includes
set(CMAKE_INCLUDE_PATH 3rd_party/include deps/include)
include_directories(SYSTEM 3rd_party/include deps/include)
include_directories(include)

# Libraries
set(CMAKE_LIBRARY_PATH deps/lib)

# Dependencies
find_path(OPENTRACING_INCLUDE_DIR NAMES opentracing/tracer.h)
find_library(OPENTRACING_LIB opentracing)
find_package(ZLIB REQUIRED)
find_library(MSGPACK_LIB msgpack)
find_package(CURL)
find_package(Threads REQUIRED)

# Code Sanitizers
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rd_party/sanitizers-cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers)

# Code
install(DIRECTORY include/datadog DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")
add_compile_options(-Wall -Wextra -Werror -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast -std=c++14)

# Outputs
set(DATADOG_LINK_LIBRARIES ${OPENTRACING_LIB} ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} Threads::Threads)

## Shared lib
if(BUILD_SHARED)
  add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing)
  target_link_libraries(dd_opentracing ${DATADOG_LINK_LIBRARIES})
  set_target_properties(dd_opentracing PROPERTIES SOVERSION ${SOVERSION})

  install(TARGETS dd_opentracing
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

## Static lib
if(BUILD_STATIC)
  add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
  add_sanitizers(dd_opentracing-static)
  set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing)
  install(TARGETS dd_opentracing-static
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
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
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
