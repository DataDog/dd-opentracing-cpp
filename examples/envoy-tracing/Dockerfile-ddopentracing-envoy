# Original from envoyproject/envoy:examples/front-proxy/Dockerfile-frontenvoy
# Modified by DataDog:
# - add install step for dd-opentracing-cpp library
FROM envoyproxy/envoy:latest

RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  DATADOG_PLUGIN_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
 wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DATADOG_PLUGIN_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz
RUN gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing.so

CMD /usr/local/bin/envoy -c /etc/front-envoy.yaml --service-cluster front-proxy
