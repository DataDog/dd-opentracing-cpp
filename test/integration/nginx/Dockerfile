FROM ubuntu:18.04 as build

# Also set in the second stage.
ARG NGINX_VERSION=1.14.0
ARG NGINX_OPENTRACING_VERSION=0.6.0

# Creates an image with nginx and the Datadog OpenTracing nginx module installed.
# Runs a simple integration test.

RUN apt-get update && \
  apt-get install -y git build-essential wget curl tar cmake libpcre3-dev zlib1g-dev

WORKDIR ~/

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
RUN cmake -DBUILD_PLUGIN=ON -DBUILD_TESTING=OFF -DBUILD_SHARED=OFF ..
RUN make
RUN make install

# Build nginx-opentracing ourselves
RUN wget https://github.com/opentracing-contrib/nginx-opentracing/archive/v${NGINX_OPENTRACING_VERSION}.tar.gz -O nginx-opentracing.tar.gz && \
  mkdir nginx-opentracing && \
  tar zxvf nginx-opentracing.tar.gz -C nginx-opentracing --strip-components=1
RUN wget http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz -O nginx-${NGINX_VERSION}.tar.gz
RUN tar zxvf nginx-${NGINX_VERSION}.tar.gz
# TODO(willgittoes-dd): Get these configure args automatically by installing nginx and using `nginx -V`.
RUN cd nginx-${NGINX_VERSION} && \
  ./configure \
  --prefix=/etc/nginx --modules-path=/usr/lib/nginx/modules \
  --user=nginx --group=nginx --with-compat --with-file-aio --with-threads \
  --with-cc-opt="-g -O2 -fdebug-prefix-map=/data/builder/debuild/nginx-${NGINX_VERSION}/debian/debuild-base/nginx-${NGINX_VERSION}=. -specs=/usr/share/dpkg/no-pie-compile.specs -fstack-protector-strong -Wformat -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -fPIC" \
  --with-ld-opt='-Wl,-Bsymbolic-functions -specs=/usr/share/dpkg/no-pie-link.specs -Wl,-z,relro -Wl,-z,now -Wl,--as-needed -pie' \
  --add-dynamic-module=../nginx-opentracing/opentracing  && \
  make  && \
  make install

FROM ubuntu:18.04

ARG NGINX_VERSION=1.14.0

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
COPY --from=build /usr/lib/nginx/modules/ngx_http_opentracing_module.so /usr/lib/nginx/modules/ngx_http_opentracing_module.so
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
RUN wget http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar -O wiremock-standalone-2.18.0.jar
RUN printf '#!/bin/bash\nset -x\njava -jar '"$(pwd)/wiremock-standalone-2.18.0.jar \"\$@\"\n" > /usr/local/bin/wiremock && \
  chmod a+x /usr/local/bin/wiremock

COPY ./test/integration/nginx/nginx_integration_test.sh ./nginx_integration_test.sh
COPY ./test/integration/nginx/expected.json ./expected.json
CMD [ "./nginx_integration_test.sh"]
