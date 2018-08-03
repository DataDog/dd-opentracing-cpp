FROM ubuntu:18.04 as build

# Creates an image with nginx and the Datadog OpenTracing nginx module installed.
# Runs a simple integration test.

RUN apt-get update && \
  apt-get install -y git build-essential wget curl tar cmake

WORKDIR ~/

COPY ./3rd_party ./dd-opentracing-cpp/3rd_party
COPY ./include ./dd-opentracing-cpp/include
COPY ./scripts ./dd-opentracing-cpp/scripts
COPY ./src ./dd-opentracing-cpp/src
COPY ./CMakeLists.txt ./dd-opentracing-cpp/CMakeLists.txt
RUN export OPENTRACING_VERSION=1.4.0 && \
  export MSGPACK_VERSION=3.0.1 && \
  export CURL_VERSION=7.60.0 && \
  cd dd-opentracing-cpp && \
  ./scripts/install_dependencies.sh
RUN rm -rf dd-opentracing-cpp/.build
RUN mkdir -p dd-opentracing-cpp/.build
WORKDIR dd-opentracing-cpp/.build
RUN cmake -DBUILD_PLUGIN=ON -DBUILD_TESTING=OFF -DBUILD_SHARED=OFF ..
RUN make
RUN make install


FROM ubuntu:18.04

ARG NGINX_VERSION=1.14.0
ARG NGINX_OPENTRACING_VERSION=0.4.0

RUN apt-get update && \
  apt-get install -y git gnupg lsb-release wget curl tar openjdk-8-jre golang jq

# Install nginx
RUN CODENAME=$(lsb_release -s -c) && \
  wget http://nginx.org/keys/nginx_signing.key && \
  apt-key add nginx_signing.key && \
  echo deb http://nginx.org/packages/ubuntu/ ${CODENAME} nginx >> /etc/apt/sources.list && \
  echo deb-src http://nginx.org/packages/ubuntu/ ${CODENAME} nginx >> /etc/apt/sources.list && \
  apt-get update && \
  apt-get install nginx=${NGINX_VERSION}-1~${CODENAME}

# Install OpenTracing
ADD https://github.com/opentracing-contrib/nginx-opentracing/releases/download/v${NGINX_OPENTRACING_VERSION}/linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz
RUN NGINX_MODULES=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--modules-path=\([^ ]*\).*/\1/p') && \
  tar zxf linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz -C ${NGINX_MODULES}
# And Datadog OT
COPY --from=build /usr/local/lib/libdd_opentracing_plugin.so /usr/local/lib/libdd_opentracing_plugin.so

# Add OpenTracing directives to nginx config
COPY ./test/integration/nginx/nginx.conf /tmp/nginx.conf
RUN NGINX_CONF=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--conf-path=\([^ ]*\).*/\1/p') && \
  mv /tmp/nginx.conf ${NGINX_CONF}
COPY ./test/integration/nginx/dd-config.json /etc/dd-config.json
RUN mkdir -p /var/www/
COPY ./test/integration/nginx/index.html /var/www/index.html

# Get Wiremock
ADD http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar wiremock-standalone-2.18.0.jar
RUN printf '#!/bin/bash\nset -x\njava -jar '"$(pwd)/wiremock-standalone-2.18.0.jar \"\$@\"\n" > /usr/local/bin/wiremock && \
  chmod a+x /usr/local/bin/wiremock

COPY ./test/integration/nginx/nginx_integration_test.sh ./nginx_integration_test.sh
COPY ./test/integration/nginx/expected.json ./expected.json
CMD [ "./nginx_integration_test.sh"]
