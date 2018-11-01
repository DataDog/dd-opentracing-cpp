FROM ubuntu:18.04

RUN apt-get update && \
  apt-get -y install build-essential cmake wget

# Download and install dd-opentracing-cpp library.
RUN wget https://github.com/DataDog/dd-opentracing-cpp/archive/v0.3.5.tar.gz -O dd-opentracing-cpp.tar.gz && \
  tar zxvf dd-opentracing-cpp.tar.gz && \
  mkdir dd-opentracing-cpp-0.3.5/.build && \
  cd dd-opentracing-cpp-0.3.5/.build && \
  # Download and install the correct version of opentracing-cpp, & other deps.
  ../scripts/install_dependencies.sh && \
  cmake .. && \
  make && \
  make install

COPY tracer_example.cpp .

RUN g++ -o tracer_example tracer_example.cpp -ldd_opentracing -lopentracing
# Add /usr/local/lib to LD_LIBRARY_PATH
RUN ldconfig

CMD ./tracer_example
