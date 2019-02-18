# Original from envoyproject/envoy:examples/front-proxy/Dockerfile-service
# Modified by DataDog:
# - add install step for dd-opentracing-cpp library
FROM envoyproxy/envoy-alpine:latest

RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  DATADOG_PLUGIN_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
 wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DATADOG_PLUGIN_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz
RUN gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing.so

RUN apk update && apk add python3 bash
RUN python3 --version && pip3 --version
RUN pip3 install -q Flask==0.11.1 requests==2.18.4
RUN mkdir /code
ADD ./examples/envoy-tracing/service.py /code
ADD ./examples/envoy-tracing/start_service.sh /usr/local/bin/start_service.sh
RUN chmod u+x /usr/local/bin/start_service.sh

ENTRYPOINT /usr/local/bin/start_service.sh
