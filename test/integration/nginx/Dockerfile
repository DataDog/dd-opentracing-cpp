FROM ubuntu

# Creates an image with nginx and the Datadog OpenTracing nginx module installed.
# Runs a simple integration test.

ARG OPENTRACING_VERSION=1.4.2
ARG MSGPACK_VERSION=3.0.1
ARG NGINX_OPENTRACING_VERSION=0.4.0
ARG NGINX_VERSION=1.15.0

RUN apt-get update && \
  apt-get install -y git build-essential libpcre3-dev zlib1g-dev libcurl4-openssl-dev wget curl tar cmake openjdk-8-jre golang jq

WORKDIR ~/

# Install opentracing dependency
ADD https://github.com/opentracing/opentracing-cpp/archive/v${OPENTRACING_VERSION}.tar.gz opentracing-cpp.tar.gz
RUN tar zxvf opentracing-cpp.tar.gz
RUN mkdir opentracing-cpp-${OPENTRACING_VERSION}/.build
WORKDIR opentracing-cpp-${OPENTRACING_VERSION}/.build
RUN cmake ..
RUN make
RUN make install
WORKDIR ../..

# Install msgpack dependency
ADD https://github.com/msgpack/msgpack-c/releases/download/cpp-${MSGPACK_VERSION}/msgpack-${MSGPACK_VERSION}.tar.gz msgpack.tar.gz
RUN tar zxvf msgpack.tar.gz
RUN mkdir msgpack-${MSGPACK_VERSION}/.build
WORKDIR msgpack-${MSGPACK_VERSION}/.build
RUN cmake ..
RUN make
RUN make install
WORKDIR ../..

# Compile dd-opentracing-cpp
COPY ./src ./dd-opentracing-cpp/src
COPY ./3rd_party ./dd-opentracing-cpp/3rd_party
COPY ./include ./dd-opentracing-cpp/include
COPY ./CMakeLists.txt ./dd-opentracing-cpp/
RUN find ./ -type f -print0 | xargs -0 md5sum # WHY CACHE BUSTING?
RUN rm -rf dd-opentracing-cpp/.build 
RUN mkdir dd-opentracing-cpp/.build
WORKDIR dd-opentracing-cpp/.build
RUN cmake -DBUILD_TESTING=OFF ..
RUN make
RUN make install
WORKDIR ../..

# Fetch and patch the nginx-opentracing module
ADD https://github.com/opentracing-contrib/nginx-opentracing/archive/v${NGINX_OPENTRACING_VERSION}.tar.gz nginx-opentracing.tar.gz
COPY ./nginx-opentracing-datadog.patch ./dd-opentracing-cpp/
RUN tar zxvf nginx-opentracing.tar.gz
WORKDIR nginx-opentracing-${NGINX_OPENTRACING_VERSION}
RUN patch -p1 < ../dd-opentracing-cpp/nginx-opentracing-datadog.patch
WORKDIR ..

# Install nginx
# Note: Installs nginx from source. In production environments you would want to install nginx from
# the official repos, and use the packaged binary of the OpenTracing module. The CI version of this
# integration test does exactly that. Here we just build from source since it's a bit more terse.
ADD http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz nginx-${NGINX_VERSION}.tar.gz
RUN tar zxvf nginx-${NGINX_VERSION}.tar.gz
WORKDIR nginx-${NGINX_VERSION}
RUN ./configure \
  --add-dynamic-module=../nginx-opentracing-${NGINX_OPENTRACING_VERSION}/opentracing \
  --add-dynamic-module=../nginx-opentracing-${NGINX_OPENTRACING_VERSION}/datadog
RUN make
RUN make install
WORKDIR ../

RUN ldconfig

# Add OpenTracing directives to nginx config
COPY ./test/integration/nginx/nginx.conf /usr/local/nginx/conf/nginx.conf

# Get Wiremock
RUN wget http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar

COPY ./test/integration/nginx/nginx_integration_test.sh ./nginx_integration_test.sh
COPY ./test/integration/nginx/expected.json ./expected.json
CMD [ "./nginx_integration_test.sh"]
