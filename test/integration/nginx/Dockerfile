ARG NGINX_VERSION=1.14.1

FROM ubuntu:18.04 as build

ARG NGINX_VERSION

ENV CFLAGS="-march=x86-64 -fPIC"
ENV CXXFLAGS="-march=x86-64 -fPIC"
ENV LDFLAGS="-fPIC"

# Creates an image with nginx and the Datadog OpenTracing nginx module installed.
# Runs a simple integration test.

RUN apt-get update && \
  apt-get install -y git build-essential wget curl tar cmake libpcre3-dev zlib1g-dev

WORKDIR /root

COPY ./scripts ./dd-opentracing-cpp/scripts
RUN cd dd-opentracing-cpp && \
  ./scripts/install_dependencies.sh
COPY ./3rd_party ./dd-opentracing-cpp/3rd_party
COPY ./include ./dd-opentracing-cpp/include
COPY ./src ./dd-opentracing-cpp/src
COPY ./CMakeLists.txt ./dd-opentracing-cpp/CMakeLists.txt
RUN rm -rf dd-opentracing-cpp/.build
RUN mkdir -p dd-opentracing-cpp/.build
WORKDIR dd-opentracing-cpp/.build
RUN cmake -DBUILD_STATIC=ON -DBUILD_TESTING=OFF -DBUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
RUN make
RUN make install
WORKDIR ..
COPY ./nginx-plugin ./nginx-plugin
RUN MODULE_DIR=/usr/local/lib/ ./scripts/build_nginx_plugin.sh


FROM ubuntu:18.04

ARG NGINX_VERSION

RUN apt-get update && \
  apt-get install -y git gnupg lsb-release wget curl tar openjdk-8-jre golang jq

# Get Wiremock
RUN wget http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar -O wiremock-standalone-2.18.0.jar
RUN printf '#!/bin/bash\nset -x\njava -jar '"$(pwd)/wiremock-standalone-2.18.0.jar \"\$@\"\n" > /usr/local/bin/wiremock && \
  chmod a+x /usr/local/bin/wiremock

# Install nginx
RUN CODENAME=$(lsb_release -s -c) && \
  wget http://nginx.org/keys/nginx_signing.key && \
  apt-key add nginx_signing.key && \
  echo deb http://nginx.org/packages/ubuntu/ ${CODENAME} nginx >> /etc/apt/sources.list && \
  echo deb-src http://nginx.org/packages/ubuntu/ ${CODENAME} nginx >> /etc/apt/sources.list && \
  apt-get update && \
  apt-get install nginx=${NGINX_VERSION}-1~${CODENAME}

# Install Datadog OpenTracing
COPY --from=build /usr/local/lib/ngx_http_dd_opentracing_module.so /etc/nginx/modules/ngx_http_dd_opentracing_module.so

# Add OpenTracing directives to nginx config
COPY ./test/integration/nginx/nginx.conf /tmp/nginx.conf
RUN NGINX_CONF=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--conf-path=\([^ ]*\).*/\1/p') && \
  mv /tmp/nginx.conf ${NGINX_CONF}
COPY ./test/integration/nginx/dd-config.json /etc/dd-config.json
RUN mkdir -p /var/www/
COPY ./test/integration/nginx/index.html /var/www/index.html

COPY ./test/integration/nginx/nginx_integration_test.sh ./nginx_integration_test.sh
COPY ./test/integration/nginx/expected.json ./expected.json
CMD [ "./nginx_integration_test.sh"]
