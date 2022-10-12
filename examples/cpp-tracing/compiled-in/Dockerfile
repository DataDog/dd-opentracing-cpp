FROM ubuntu:20.04

RUN apt-get update && \
  DEBIAN_FRONTEND=noninteractive apt-get -y install build-essential cmake wget coreutils

# Download and install dd-opentracing-cpp library.
RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
  wget https://github.com/DataDog/dd-opentracing-cpp/archive/${VERSION}.tar.gz -O dd-opentracing-cpp.tar.gz && \
  mkdir -p dd-opentracing-cpp/.build && \
  tar zxvf dd-opentracing-cpp.tar.gz -C ./dd-opentracing-cpp/ --strip-components=1 && \
  cd dd-opentracing-cpp/.build && \
  # Download and install the correct version of opentracing-cpp, & other deps.
  ../scripts/install_dependencies.sh && \
  cmake .. && \
  make -j "$(nproc)" && \
  make install

COPY tracer_example.cpp .

RUN g++ -std=c++14 -o tracer_example tracer_example.cpp -I/dd-opentracing-cpp/deps/include -L/dd-opentracing-cpp/deps/lib -ldd_opentracing -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

CMD sleep 5 && ./tracer_example && sleep 25
