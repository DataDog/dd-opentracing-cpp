[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg)](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master)

# Datadog OpenTracing C++ Client

**Notice: This project is still in beta, under active development. Features and compatibility may change.**

- [Datadog OpenTracing C++ Client](#datadog-opentracing-c-client)
  - [Usage](#usage)
    - [Tracing C++ Applications](#tracing-c-applications)
      - [Getting Started](#getting-started)
      - [Compatibility](#compatibility)
      - [Installation](#installation)
        - [Compile against dd-opentracing-cpp](#compile-against-dd-opentracing-cpp)
        - [Dynamic Loading](#dynamic-loading)
      - [Advanced Usage](#advanced-usage)
        - [OpenTracing](#opentracing)
        - [Manual Instrumentation](#manual-instrumentation)
        - [Custom Tagging](#custom-tagging)
        - [Distributed Tracing](#distributed-tracing)
        - [Priority Sampling](#priority-sampling)
        - [Logging](#logging)
        - [Debugging](#debugging)
    - [Tracing Nginx](#tracing-nginx)
      - [Quick-start with Docker example](#quick-start-with-docker-example)
      - [Guide](#guide)
    - [Tracing Envoy & Istio](#tracing-envoy--istio)
  - [Building](#building)

## Usage

### Tracing C++ Applications

#### Getting Started

To begin tracing applications written in any language, first [install and configure the Datadog Agent](https://docs.datadoghq.com/tracing/setup).

You will need to compile against [OpenTracing-cpp](https://github.com/opentracing/opentracing-cpp).

#### Compatibility

dd-opentracing-cpp needs C++14 to build, but if you use [dynamic loading](#dynamic-loading) then you are instead limited by OpenTracing's requirement for [C++11 or later](https://github.com/opentracing/opentracing-cpp/#cc98).

Supported platforms are: Linux & Mac. If you need Windows support, please let us know.

#### Installation

Datadog tracing can be enabled in one of two ways:

* Compile against dd-opentracing-cpp, where the Datadog lib is compiled in and configured in code
* Dynamic loading, where the Datadog OpenTracing library is loaded at run-time and configured via JSON

##### Compile against dd-opentracing-cpp

```bash
# Download and install dd-opentracing-cpp library.
wget https://github.com/DataDog/dd-opentracing-cpp/archive/v0.3.5.tar.gz -O dd-opentracing-cpp.tar.gz
tar zxvf dd-opentracing-cpp.tar.gz
mkdir dd-opentracing-cpp-0.3.5/.build
cd dd-opentracing-cpp-0.3.5/.build
# Download and install the correct version of opentracing-cpp, & other deps.
../scripts/install_dependencies.sh
cmake ..
make
make install
```

Include `<datadog/opentracing.h>` and create the tracer:

```cpp
// tracer_example.cpp
#include <datadog/opentracing.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  datadog::opentracing::TracerOptions tracer_options{"dd-agent", 8126, "compiled-in example"};
  auto tracer = datadog::opentracing::makeTracer(tracer_options);

  // Create some spans.
  {
    auto span_a = tracer->StartSpan("A");
    span_a->SetTag("tag", 123);
    auto span_b = tracer->StartSpan("B", {opentracing::ChildOf(&span_a->context())});
    span_b->SetTag("tag", "value");
  }

  tracer->Close();
  return 0;
}
```

Just link against libdd_opentracing and libopentracing (making sure that they are both in your LD_LIBRARY_PATH):

```bash
g++ -o tracer_example tracer_example.cpp -ldd_opentracing -lopentracing
./tracer_example
```

##### Dynamic Loading

```bash
# Download and install OpenTracing-cpp
wget https://github.com/opentracing/opentracing-cpp/archive/v1.5.0.tar.gz -O opentracing-cpp.tar.gz
tar zxvf opentracing-cpp.tar.gz
mkdir opentracing-cpp-1.5.0/.build
cd opentracing-cpp-1.5.0/.build
cmake ..
make
make install
# Install dd-opentracing-cpp shared plugin.
wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/v0.3.5/linux-amd64-libdd_opentracing_plugin.so.gz
gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so
```

Include `<opentracing/dynamic_load.h>` and load the tracer from libdd_opentracing_plugin.so:

```cpp
// tracer_example.cpp
#include <opentracing/dynamic_load.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  // Load the tracer library.
  std::string error_message;
  auto handle_maybe = opentracing::DynamicallyLoadTracingLibrary(
      "/usr/local/lib/libdd_opentracing_plugin.so", error_message);
  if (!handle_maybe) {
    std::cerr << "Failed to load tracer library " << error_message << "\n";
    return false;
  }

  // Read in the tracer's configuration.
  std::string tracer_config = R"({
      "service": "dynamic-load example",
      "agent_host": "dd-agent",
      "agent_port": 8126
    })";

  // Construct a tracer.
  auto& tracer_factory = handle_maybe->tracer_factory();
  auto tracer_maybe = tracer_factory.MakeTracer(tracer_config.c_str(), error_message);
  if (!tracer_maybe) {
    std::cerr << "Failed to create tracer " << error_message << "\n";
    return false;
  }
  auto& tracer = *tracer_maybe;

  // Create some spans.
  {
    auto span_a = tracer->StartSpan("A");
    span_a->SetTag("tag", 123);
    auto span_b = tracer->StartSpan("B", {opentracing::ChildOf(&span_a->context())});
    span_b->SetTag("tag", "value");
  }

  tracer->Close();
  return 0;
}
```

Just link against libopentracing (making sure that libopentracing.so is in your LD_LIBRARY_PATH):

```bash
g++ -o tracer_example tracer_example.cpp -lopentracing
./tracer_example
```

#### Advanced Usage

##### OpenTracing

The Datadog C++ tracer currently can only be used through the OpenTracing API. The usage instructions in this document all describe generic OpenTracing functionality.

##### Manual Instrumentation

To manually instrument your code, install using one of the above methods and then use the tracer object to create Spans.

```cpp
{
  // Create a root span.
  auto root_span = tracer->StartSpan("operation_name");
  // Create a child span.
  auto child_span = tracer->StartSpan(
      "operation_name",
      {opentracing::ChildOf(&root_span->context())});
  // Spans can be finished at a specific time ...
  child_span->Finish();
} // ... or when they are destructed (root_span finishes here).
```

##### Custom Tagging

Add tags directly to a Span object by calling Span.SetTag(). For example:

```cpp
auto tracer = ...
auto span = tracer->StartSpan("operation_name");
span->SetTag("key must be string", "Values are variable types");
span->SetTag("key must be string", 1234);
```

Values are of [variable type](https://github.com/opentracing/opentracing-cpp/blob/master/include/opentracing/value.h) and can be complex objects. Values are serialized as JSON, with the exception of a string value being serialized bare (without extra quotation marks).

##### Distributed Tracing

Distributed tracing can be accomplished by [using the Inject and Extract methods on the tracer](https://github.com/opentracing/opentracing-cpp/#inject-span-context-into-a-textmapwriter), which accept [generic `Reader` and `Writer` types](https://github.com/opentracing/opentracing-cpp/blob/master/include/opentracing/propagation.h). Priority sampling (enabled by default) should be on to ensure uniform delivery of spans.

```cpp
// Allows writing propagation headers to a simple map<string, string>.
// Copied from https://github.com/opentracing/opentracing-cpp/blob/master/mocktracer/test/propagation_test.cpp
struct HTTPHeadersCarrier : HTTPHeadersReader, HTTPHeadersWriter {
  HTTPHeadersCarrier(std::unordered_map<std::string, std::string>& text_map_)
      : text_map(text_map_) {}

  expected<void> Set(string_view key, string_view value) const override {
    text_map[key] = value;
    return {};
  }

  expected<void> ForeachKey(
      std::function<expected<void>(string_view key, string_view value)> f)
      const override {
    for (const auto& key_value : text_map) {
      auto result = f(key_value.first, key_value.second);
      if (!result) return result;
    }
    return {};
  }

  std::unordered_map<std::string, std::string>& text_map;
};

void example() {
  auto tracer = ...
  std::unordered_map<std::string, std::string> headers;
  HTTPHeadersCarrier carrier(headers);

  auto span = tracer->StartSpan("operation_name");
  tracer->Inject(span->context(), carrier);
  // `headers` now populated with the headers needed to propagate the span.
}
```

##### Priority Sampling

Priority sampling is enabled by default, and can be disabled in the TracerOptions. You can mark a span to be kept or discarded by setting the tag `sampling.priority`. A value of `0` means reject/don't sample and any value greater than 0 means keep/sample.

```cpp
auto tracer = ...
auto span = tracer->StartSpan("operation_name");
span->SetTag("sampling.priority", 1); // Keep this span.
auto another_span = tracer->StartSpan("operation_name");
another_span->SetTag("sampling.priority", 0); // Discard this span.
```

##### Logging

Coming soon!

##### Debugging

The release binary libraries are all compiled with debug symbols added to the optimized release. It is possible to use gdb or lldb to debug the library and to read core dumps. If you are building the library from source, pass the argument `-DCMAKE_BUILD_TYPE=RelWithDebInfo` to cmake to compile an optimized build with debug symbols.

```bash
cd .build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make
make install
```

### Tracing Nginx

Nginx can be traced using the nginx-opentracing module along with this library.

#### Quick-start with Docker example

1. Put your Datadog API key in examples/nginx-tracing/docker-compose.yml
2. `cd examples/nginx-tracing`
3. `docker-compose up --build`
4. Visit http://localhost:8080
5. Observe traces in Datadog APM under service name "nginx".

#### Guide

Explains how the Docker example works.

Nginx tracing is compatible with the nginx binary package from the official nginx [repositories](http://nginx.org/en/linux_packages.html#stable). eg.

```bash
wget https://nginx.org/keys/nginx_signing.key
apt-key add nginx_signing.key
echo deb https://nginx.org/packages/ubuntu/ bionic nginx >> /etc/apt/sources.list
echo deb-src https://nginx.org/packages/ubuntu/ bionic nginx >> /etc/apt/sources.list
apt-get update
apt-get install nginx=1.14.0-1~bionic # <- Your Ubuntu distro here
```

Two dynamic libraries need to be available:

* ngx_http_opentracing_module.so, provided by [nginx-opentracing](https://github.com/opentracing-contrib/nginx-opentracing/)
* libdd_opentracing_plugin.so, from this repo

Each of these can be downloaded and used precompiled.

```bash
# Install OpenTracing nginx module
wget https://github.com/opentracing-contrib/nginx-opentracing/releases/download/v0.7.0/linux-amd64-nginx-1.14.0-ngx_http_module.so.tgz
tar zxf linux-amd64-nginx-1.14.0-ngx_http_module.so.tgz -C /usr/lib/nginx/modules
# Install Datadog OpenTracing
wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/v0.3.5/linux-amd64-libdd_opentracing_plugin.so.gz
gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so
```

Tracing is configured in two locations:

* Your nginx config.
* A datadog-specific JSON-formatted config file. This can be placed anywhere readable by nginx, and is referenced in the nginx config file.

Annotated nginx config file:

```nginx
load_module modules/ngx_http_opentracing_module.so; # Load OpenTracing module

events {
    worker_connections  1024;
}

http {
    opentracing on; # Enable OpenTracing
    opentracing_tag http_user_agent $http_user_agent; # Add a tag to each trace!
    opentracing_trace_locations off; # Emit only one span per request.

    # Load the Datadog tracing implementation, and the given config file.
    opentracing_load_tracer /usr/local/lib/libdd_opentracing_plugin.so /etc/dd-config.json;

    server {
        listen       80;
        server_name  localhost;

        location /test {
            # Enable tracing for this location block and set the operation name.
            opentracing_operation_name "$request_method $uri";
            # Set the resource for the span.
            opentracing_tag "resource.name" "/test";
            root   /var/www;
        }
    }
}
```

Annotated Datadog config JSON:

```javascript
// Note, not valid JSON. JSON may not have comments!
{
  // The only required field. Sets the service name.
  "service": "nginx",
  // Not required but highly reccommended option. Normalises span names so that all nginx traces can be found easily in the Datadog UI, replacing the OpenTracing operation name with the value provided here (keeping the operation name as a tag). The opentracing_tag nginx directive can still be used to set the "resource.name" tag to set resource names.
  "operation_name_override": "nginx.handle",
  // These define the address of the trace agent. The default values are below.
  "agent_host": "localhost",
  "agent_port": 8126,
  // Client-side sampling. Discards (without counting) some number of traces where 1.0 means "keep all traces" and 0.0 means "keep no traces". Useful for improving performance in the case where nginx receives a large number of very small requests. Default value is 1.0 / keep everything.
  "sample_rate": 1.0,
  // Priority sampling. A boolean, true by default. If true disables client-side sampling (thus ignoring sample_rate)
  // and enables distributed priority sampling, where traces are sampled based on a combination of user-assigned
  // priorities and configuration from the agent.
  "dd.priority.sampling": true,
  // A list of strings, each string is one of "Datadog", "B3". Defaults to ["Datadog", "B3"]. The type of headers
  // to use to propagate distributed traces.
  "propagation_style_extract": ["Datadog", "B3"],
  // A list of strings, each string is one of "Datadog", "B3". Defaults to ["Datadog"]. The type of headers to use
  // to receive distributed traces. 
  "propagation_style_inject": ["Datadog"]
}
```

You also need to provide a JSON-formatted text config file that sets options for the Datadog tracing.

### Tracing Envoy & Istio

Coming soon!

## Building

**Dependencies**

- cmake >= 3.0
- Build tools (eg. build-essential, xcode)

See scripts/install_dependencies.sh

**Build steps**

First install dependencies:

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
