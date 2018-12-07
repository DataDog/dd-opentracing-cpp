FROM ubuntu:18.04

RUN apt-get update && \
  apt-get -y install build-essential cmake wget

# Download and install OpenTracing-cpp
RUN wget https://github.com/opentracing/opentracing-cpp/archive/v1.5.0.tar.gz -O opentracing-cpp.tar.gz && \
  tar zxvf opentracing-cpp.tar.gz && \
  mkdir opentracing-cpp-1.5.0/.build && \
  cd opentracing-cpp-1.5.0/.build && \
  cmake .. && \
  make && \
  make install && \
  # Install dd-opentracing-cpp shared plugin.
  wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/v0.3.7/linux-amd64-libdd_opentracing_plugin.so.gz && \
  gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so


COPY tracer_example.cpp .

RUN g++ -o tracer_example tracer_example.cpp -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

CMD sleep 5 && ./tracer_example && sleep 25
