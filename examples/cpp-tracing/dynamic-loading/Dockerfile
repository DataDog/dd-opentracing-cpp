FROM ubuntu:14.04

RUN apt-get update && \
  apt-get -y install build-essential cmake3 wget gdb git software-properties-common && \
  add-apt-repository ppa:ubuntu-toolchain-r/test && \
  apt-get update && \
  apt-get -y install gcc-5 g++-5 && \
  update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5 60 --slave /usr/bin/g++ g++ /usr/bin/g++-5

# Download and install OpenTracing-cpp
RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  DD_OPENTRACING_CPP_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
  OPENTRACING_VERSION="$(get_latest_release opentracing/opentracing-cpp)" && \
  wget https://github.com/opentracing/opentracing-cpp/archive/${OPENTRACING_VERSION}.tar.gz -O opentracing-cpp.tar.gz && \
  mkdir -p opentracing-cpp/.build && \
  tar zxvf opentracing-cpp.tar.gz -C ./opentracing-cpp/ --strip-components=1 && \
  cd opentracing-cpp/.build && \
  cmake .. && \
  make && \
  make install && \
  # Install dd-opentracing-cpp shared plugin.
  wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DD_OPENTRACING_CPP_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz && \
  gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so


COPY tracer_example.cpp .
COPY tracer_example_string.cpp .
COPY tracer_example_char.cpp .
COPY run_things.sh .

RUN g++ -g -std=c++11 -o tracer_example tracer_example.cpp -lopentracing
RUN g++ -g -std=c++11 -o tracer_example_string tracer_example_string.cpp -lopentracing
RUN g++ -g -std=c++11 -o tracer_example_char tracer_example_char.cpp -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

CMD ./run_things.sh
