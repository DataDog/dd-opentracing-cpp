cmake_minimum_required (VERSION 3.0)
set(CMAKE_CXX_STANDARD 14)

project(dd-opentracing-cpp)
set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")


configure_file (
  "${PROJECT_SOURCE_DIR}/src/version_number.h.in"
  "${PROJECT_BINARY_DIR}/version_number.h"
  )
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
