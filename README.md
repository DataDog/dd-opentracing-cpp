[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg)](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master)

# Datadog OpenTracing C++ Client

## Usage

Usage docs are on the main Datadog website:

* [NGINX](https://docs.datadoghq.com/tracing/setup/nginx/)
* [Envoy](https://docs.datadoghq.com/tracing/setup/envoy/)
* [Istio](https://docs.datadoghq.com/tracing/setup/istio/)
* [C++ code](https://docs.datadoghq.com/tracing/setup/cpp/)

For some quick-start examples, see the [examples](examples/) folder.

## Contributing

Before considering contributions to the project, please take a moment to read our brief [contribution guidelines](CONTRIBUTING.md).

## Build and Test

### Dependencies

Building this project requires the following tools installed:
- Build tools (eg. `build-essential`, xcode)
- `cmake` >= 3.1

Additional libraries are installed via a script.

### Build Steps

- Clone the repository
```
git clone https://github.com/DataDog/dd-opentracing-cpp
```
- Install additional library dependencies (requires `sudo`)
```
cd dd-opentracing-cpp
sudo scripts/install_dependencies.sh
```
- Generate build files using cmake
```
mkdir .build
cd .build
cmake ..
```
- Run the build
```
make
```
- (Optional) Run the tests
```
cmake -DBUILD_TESTING=ON ..
make
ctest --output-on-failure
```
- (Optional) Install in `/usr/local`
```
make install
```

If you want [sanitizers](https://github.com/google/sanitizers) to be enabled, then add either the `-DSANITIZE_THREAD=ON -DSANITIZE_UNDEFINED=ON` or `-DSANITIZE_ADDRESS=ON` flags to cmake, running the tests will now also check with the sanitizers.

### Integration tests

Integration tests require additional tools installed:
- [msgpack-cli](https://github.com/jakm/msgpack-cli)
- [wiremock](https://github.com/tomakehurst/wiremock)
- `jq`

Installation details can be extracted from the [Dockerfile](https://github.com/DataDog/docker-library/blob/master/dd-opentracing-cpp/test/0.3.1/Dockerfile#L7-L14) for the container that is usually used when running integration tests.

The script below will run the integration tests directly.
```
test/integration/run_integration_tests_local.sh
```
