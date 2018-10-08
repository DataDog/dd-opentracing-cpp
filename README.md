[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg)](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master)

# Datadog OpenTracing C++ Client

**Notice: This project is still in beta, under active development. Features and compatibility may change.**

* [Usage](#usage)
   * [Tracing nginx](#tracing-nginx)
      * [Quick start/Example](#quick-start-with-docker-example)
      * [Guide](#guide)
* [Development](#building)

## Usage

### Tracing C++ Code

Support coming soon.

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
wget https://github.com/opentracing-contrib/nginx-opentracing/releases/download/v0.6.0/linux-amd64-nginx-1.14.0-ngx_http_module.so.tgz
tar zxf linux-amd64-nginx-1.14.0-ngx_http_module.so.tgz -C /usr/lib/nginx/modules
# Install Datadog OpenTracing
wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/v0.3.1/linux-amd64-libdd_opentracing_plugin.so.gz
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
  "sample_rate": 1.0
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
