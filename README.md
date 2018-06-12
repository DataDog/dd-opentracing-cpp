[![CircleCI](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master.svg?style=svg&circle-token=ad1c0acf298d39a594751ac9eb76ea92243e5fe3)](https://circleci.com/gh/DataDog/dd-opentracing-cpp/tree/master)

# Datadog OpenTracing C++ Client

**Notice: This project is still in beta, under active development. Features and compatibility may change.**

## Building

**Dependencies**

- cmake >= 3.0
- [OpenTracing C++](https://github.com/opentracing/)
- [msgpack-c](ttps://github.com/msgpack/msgpack-c/)
- [libCURL](https://curl.haxx.se/libcurl/)
- Build tools (eg. build-essential, xcode)

**Build steps**

    mkdir .build
    cd .build
    cmake ..
    make
    make install

**Running the tests**

Either just run `circleci build` or:

    mkdir .build
    cd .build
    cmake ..
    make
    ctest --output-on-failure

`make test` also works instead of calling ctest, but [doesn't print](https://stackoverflow.com/questions/5709914/using-cmake-how-do-i-get-verbose-output-from-ctest) which tests are failing.

If you want [sanitizers](https://github.com/google/sanitizers) to be enabled, then instead of `cmake ..`, use either `cmake -DSANITIZE_THREAD=On -DSANITIZE_UNDEFINED=On ..` or `cmake -DSANITIZE_ADDRESS=On ..`, running the tests will now also check with the sanitizers.

## Usage

### Tracing C++ Code

Support coming soon.

### Tracing Nginx

Currently nginx needs compilation against any modules used, although there may be future support for hot-loading dynamically-linked modules.

#### Quick-start with Docker

1. Get a Datadog agent up and running
2. Modify nginx-tracing/nginx.conf to set the `datadog_agent_host` and `datadog_agent_port`
3. `docker-compose build`
4. `docker-compose up`
5. Visit http://localhost:8080

#### Build it yourself

First, [build and install](#building) the Datadog OpenTracing C++ client.

    git clone git@github.com:DataDog/dd-opentracing-cpp.git
    # and then build as above

Download the [nginx-opentracing](https://github.com/opentracing-contrib/nginx-opentracing/releases) module code

    wget https://github.com/opentracing-contrib/nginx-opentracing/archive/v0.2.1.tar.gz
    tar zxvf nginx-opentracing-0.2.1.tar.gz

*Note: The next step only required until the Datadog opentracing-nginx patch is merged*

Patch nginx-opentracing to add Datadog support

     cd nginx-opentracing-0.2.1
     patch -p1 < ../dd-opentracing-cpp/nginx-opentracing-datadog.patch
     cd ../

Install nginx dependencies, eg

    apt-get update
    apt-get install -y git build-essential libpcre3-dev zlib1g-dev libcurl4-openssl-dev wget curl tar cmake

Download nginx source

    wget http://nginx.org/download/nginx-1.13.10.tar.gz
    tar zxvf nginx-1.13.10.tar.gz

Build and install nginx

    cd nginx-1.13.10
    ./configure \
        --add-dynamic-module=../nginx-opentracing-0.2.1/opentracing \
        --add-dynamic-module=../nginx-opentracing-0.2.1/datadog
    make
    make install
    cd ..

Create an nginx config that loads and uses the module.

    cat <<EOT >> nginx.conf
    load_module modules/ngx_http_opentracing_module.so;
    load_module modules/ngx_http_datadog_module.so;

    events {
        worker_connections  1024;
    }

    http {
        opentracing on;
        opentracing_tag http_user_agent $http_user_agent;
        datadog_agent_host localhost; # IP address or hostname of agent
        datadog_agent_port 8129;

        server {
            listen       80;
            server_name  localhost;

            location / {
                opentracing_operation_name $uri;
                root   html;
                index  index.html index.htm;
            }

            location /test {
                opentracing_operation_name $uri;
                root   html;
                index  index.html index.htm;
            }
        }
    }
    EOT

Run nginx with that configuration

    nginx -c nginx.conf
