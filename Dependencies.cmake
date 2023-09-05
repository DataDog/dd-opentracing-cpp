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
  "WITH_STL OFF" "WITH_OPENTRACING ON" "BUILD_TESTING OFF" "BUILD_STATIC_LIBS ON" "WITH_EXAMPLES OFF"
)
CPMAddPackage(
  NAME curl
  GITHUB_REPOSITORY curl/curl
  GIT_TAG curl-8_2_1
  OPTIONS 
  "HTTP_ONLY ON" "CURL_ENABLE_SSL OFF" "BUILD_CURL_EXE OFF" "BUILD_SHARED_LIBS OFF" "CMAKE_POSITION_INDEPENDENT_CODE ON"

)
CPMAddPackage(
  NAME msgpack-c 
  GITHUB_REPOSITORY msgpack/msgpack-c
  GIT_TAG cpp-6.1.0
  OPTIONS "MSGPACK_CXX14 ON" "MSGPACK_USE_BOOST OFF" "BUILD_SHARED_LIBS OFF"
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
