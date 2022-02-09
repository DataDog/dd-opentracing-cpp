# Datadog OpenTracing C++ Client

[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg)](https://app.circleci.com/pipelines/github/DataDog/dd-opentracing-cpp?branch=master)

## Usage

Usage docs are on the main Datadog website:

* [NGINX](https://docs.datadoghq.com/tracing/setup/nginx/)
* [Envoy](https://docs.datadoghq.com/tracing/setup/envoy/)
* [Istio](https://docs.datadoghq.com/tracing/setup/istio/)
* [C++ code](https://docs.datadoghq.com/tracing/setup/cpp/)

For some quick-start examples, see the [examples](examples/) folder.

## Contributing

Before considering contributions to the project, please take a moment to read our brief [contribution guidelines](CONTRIBUTING.md).

## Build and Test (Linux and macOS)

### Dependencies

Building this project requires the following tools installed:

- Build tools (e.g. `build-essential`, xcode)
- `cmake` >= 3.1

Additional libraries are installed via a script.

### Build Steps

- Clone the repository
    ```sh
    git clone https://github.com/DataDog/dd-opentracing-cpp
    ```
- Install additional library dependencies (requires `sudo`)
    ```sh
    cd dd-opentracing-cpp
    sudo scripts/install_dependencies.sh
    ```
- Generate build files using `cmake`
    ```sh
    mkdir .build
    cd .build
    cmake ..
    ```
- Run the build
    ```sh
    make
    ```
- (Optional) Run the tests
    ```sh
    cmake -DBUILD_TESTING=ON ..
    make
    ctest --output-on-failure
    ```
- (Optional) Install to `/usr/local`
    ```sh
    make install
    ```

If you want [sanitizers](https://github.com/google/sanitizers) to be enabled, then add either the `-DSANITIZE_THREAD=ON -DSANITIZE_UNDEFINED=ON` or `-DSANITIZE_ADDRESS=ON` flags to cmake, running the tests will now also check with the sanitizers.

You can enable code coverage instrumentation in the builds of the library and its unit tests by adding the `-DBUILD_COVERAGE=ON` flag to cmake. See [scripts/run_coverage.sh](scripts/run_coverage.sh).

### Build (Windows)

**NOTE**: This is currently Early Access, and issues should be reported only via GitHub Issues. Installation steps are likely to change based on user feedback and becoming available via Vcpkg.

### Dependencies

Building this project requires the following tools installed:

- Visual Studio 2019 with "Desktop development for C++" installed
- Vcpkg
- Git

### Build Steps

The commands below should be executed in an `x64 Native Tools Command Prompt` shell.

- Clone the repository
    ```sh
    cd %HOMEPATH%
    git clone https://github.com/DataDog/dd-opentracing-cpp
    ```
- Generate build files using `cmake`
    ```bat
    cd dd-opentracing-cpp
    mkdir .build
    cd .build
    cmake -DCMAKE_TOOLCHAIN_FILE=%HOMEPATH%\vcpkg\scripts\buildsystems\vcpkg.cmake ..
    ```
- Run the build
    ```bat
    cmake --build . -- -p:Configuration=RelWithDebInfo
    ```

Take care to update the `Configuration` value (e.g. to `Debug`) if you change
the build mode in your IDE.  See this [related issue][1].

### Integration tests

Integration tests require additional tools installed:

- [msgpack-cli](https://github.com/jakm/msgpack-cli)
- [wiremock](https://github.com/tomakehurst/wiremock)
- `jq`

Installation details can be extracted from the [Dockerfile](https://github.com/DataDog/docker-library/blob/master/dd-opentracing-cpp/test/0.3.1/Dockerfile#L7-L14) for the container that is usually used when running integration tests.

Run this command to run the integration tests directly.

```sh
test/integration/run_integration_tests_local.sh
```

[1]: https://github.com/DataDog/dd-opentracing-cpp/issues/170
