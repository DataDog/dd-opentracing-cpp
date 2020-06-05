# An nginx container that includes the module and plugin required for Datadog tracing.
FROM nginx:1.17.3

ARG NGINX_VERSION=1.17.3

RUN apt-get update && \
  apt-get install -y wget tar

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
