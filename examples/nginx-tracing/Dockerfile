# Builds and runs a simple nginx server, traced by Datadog
FROM ubuntu:18.04

ARG NGINX_VERSION=1.14.0

RUN apt-get update && \
  apt-get install -y git gnupg wget tar

# Install nginx
RUN wget https://nginx.org/keys/nginx_signing.key && \
  apt-key add nginx_signing.key && \
  echo deb https://nginx.org/packages/ubuntu/ bionic nginx >> /etc/apt/sources.list && \
  echo deb-src https://nginx.org/packages/ubuntu/ bionic nginx >> /etc/apt/sources.list && \
  apt-get update && \
  apt-get install nginx=${NGINX_VERSION}-1~bionic
# Configure nginx
COPY ./examples/nginx-tracing/nginx.conf /etc/nginx/nginx.conf
COPY ./examples/nginx-tracing/dd-config.json /etc/dd-config.json
RUN mkdir -p /var/www/
COPY ./examples/nginx-tracing/index.html /var/www/index.html

# Install nginx-opentracing
RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  OPENTRACING_NGINX_VERSION="$(get_latest_release opentracing-contrib/nginx-opentracing)" && \
  DD_OPENTRACING_CPP_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
  \
  wget https://github.com/opentracing-contrib/nginx-opentracing/releases/download/${OPENTRACING_NGINX_VERSION}/linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz && \
  NGINX_MODULES=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--modules-path=\([^ ]*\).*/\1/p') && \
  tar zxvf linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz -C "${NGINX_MODULES}" && \
  # Install Datadog module
  wget -O - https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DD_OPENTRACING_CPP_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz | gunzip -c > /usr/local/lib/libdd_opentracing_plugin.so

# Test nginx config.
RUN nginx -t

EXPOSE 80
CMD [ "nginx", "-g", "daemon off;"]
