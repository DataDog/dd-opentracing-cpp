cmake_minimum_required (VERSION 3.0)
set(CMAKE_CXX_STANDARD 14)

project (dd-opentracing-cpp)

# Dependencies
find_path(OPENTRACING_INCLUDE_DIR NAMES opentracing/tracer.h)
find_library(OPENTRACING_LIB opentracing)

# Includes
include_directories(SYSTEM ${OPENTRACING_INCLUDE_DIR})
include_directories(include)

# Code
install(DIRECTORY include/datadog DESTINATION include)
file(GLOB DD_OPENTRACING_SOURCES "src/*.cpp")

# Outputs
## Shared lib
add_library(dd_opentracing SHARED ${DD_OPENTRACING_SOURCES})
target_link_libraries(dd_opentracing ${OPENTRACING_LIB})

## Static lib
add_library(dd_opentracing-static STATIC ${DD_OPENTRACING_SOURCES})
set_target_properties(dd_opentracing-static PROPERTIES OUTPUT_NAME dd_opentracing)
target_link_libraries(dd_opentracing-static ${OPENTRACING_LIB} datadog)

install(TARGETS dd_opentracing dd_opentracing-static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
