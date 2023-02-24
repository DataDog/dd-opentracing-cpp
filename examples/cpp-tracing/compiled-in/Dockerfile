FROM ubuntu:18.04

RUN apt-get update && \
  apt-get -y install build-essential cmake wget

# Download and install dd-opentracing-cpp library.
RUN wget https://github.com/DataDog/dd-opentracing-cpp/archive/v1.2.1.tar.gz -O dd-opentracing-cpp.tar.gz && \
  mkdir -p dd-opentracing-cpp/.build && \
  tar zxvf dd-opentracing-cpp.tar.gz -C ./dd-opentracing-cpp/ --strip-components=1 && \
  cd dd-opentracing-cpp/.build && \
  # Download and install the correct version of opentracing-cpp, & other deps.
  ZLIB_VERSION=1.2.13 ../scripts/install_dependencies.sh && \
  cmake .. && \
  make && \
  make install

COPY tracer_example.cpp .

RUN g++ -std=c++14 -o tracer_example tracer_example.cpp -I/dd-opentracing-cpp/deps/include -L/dd-opentracing-cpp/deps/lib -ldd_opentracing -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

CMD ./tracer_example
