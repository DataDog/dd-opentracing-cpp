include(cmake/CPM.cmake)
set(CPM_USE_LOCAL_PACKAGES ON)

function(add_external_dependencies)

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
  OPTIONS "MSGPACK_CXX20 ON" "MSGPACK_USE_BOOST OFF"
)
CPMAddPackage(
  NAME nlohmann_json
  VERSION 3.11.2
  GITHUB_REPOSITORY nlohmann/json
  GITHUB_TAG v3.11.2
  OPTIONS
    "JSON_BuildTests OFF"
)

endfunction()