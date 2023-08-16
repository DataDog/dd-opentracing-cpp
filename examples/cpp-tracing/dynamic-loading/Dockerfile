FROM ubuntu:20.04

RUN apt-get update && \
  DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends install build-essential cmake ca-certificates wget coreutils jq

# Download and install OpenTracing-cpp
RUN wget https://github.com/opentracing/opentracing-cpp/archive/v1.6.0.tar.gz -O opentracing-cpp.tar.gz && \
  mkdir -p opentracing-cpp/.build && \
  tar zxvf opentracing-cpp.tar.gz -C ./opentracing-cpp/ --strip-components=1 && \
  cd opentracing-cpp/.build && \
  cmake .. && \
  make -j "$(nproc)" && \
  make install
# Install dd-opentracing-cpp plugin
RUN wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/v1.3.7/linux-amd64-libdd_opentracing_plugin.so.gz && \
  gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so


COPY tracer_example.cpp .

RUN g++ -std=c++14 -o tracer_example tracer_example.cpp -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

USER nobody
CMD sleep 5 && ./tracer_example && sleep 25
