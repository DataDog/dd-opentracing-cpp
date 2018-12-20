[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg)](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master)

# Datadog OpenTracing C++ Client

**Notice: This project is still in beta, under active development. Features and compatibility may change.**

- [Datadog OpenTracing C++ Client](#datadog-opentracing-c-client)
  - [Usage](#usage)
  - [Contributor Info](#contributor-info)

## Usage

Usage docs are on the main Datadog website:

* [Envoy](https://docs.datadoghq.com/tracing/proxies/envoy/)
* [NGINX](https://docs.datadoghq.com/tracing/proxies/nginx/)
* [C++ code](https://docs.datadoghq.com/tracing/languages/cpp/)

For some quick-start examples, see the [examples](examples/) folder.

## Contributor Info

**Dependencies**

- cmake >= 3.1
- Build tools (eg. build-essential, xcode)
- libz, libcurl, libmsgpack, libopentracing (automatically installed by scripts/install_dependencies.sh)

**Build steps**

First init submodules and install dependencies:

    git submodule update --init --recursive
    scripts/install_dependencies.sh

Then:

    mkdir .build
    cd .build
    cmake ..
    make
    make install

**Running the tests**

    mkdir .build
    cd .build
    cmake -DBUILD_TESTING=ON ..
    make
    ctest --output-on-failure

`make test` also works instead of calling ctest, but [doesn't print](https://stackoverflow.com/questions/5709914/using-cmake-how-do-i-get-verbose-output-from-ctest) which tests are failing.

If you want [sanitizers](https://github.com/google/sanitizers) to be enabled, then add either the `-DSANITIZE_THREAD=ON -DSANITIZE_UNDEFINED=ON` or `-DSANITIZE_ADDRESS=ON` flags to cmake, running the tests will now also check with the sanitizers.

**Running integration/e2e tests**

    ./test/integration/run_integration_tests_local.sh
