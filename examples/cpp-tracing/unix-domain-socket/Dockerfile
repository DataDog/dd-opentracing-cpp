from ubuntu:20.04

run apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install build-essential cmake wget

# Download and install the latest release of the Datadog C++ tracer library.
copy bin/install-latest-dd-opentracing-cpp .
run ./install-latest-dd-opentracing-cpp

copy tracer_example.cpp .

# Compile the tracer client.
run g++ \
  -std=c++14 \
  -o tracer_example \
  tracer_example.cpp \
  -I/dd-opentracing-cpp/deps/include \
  -L/dd-opentracing-cpp/deps/lib \
  -ldd_opentracing \
  -lopentracing

# Add /usr/local/lib to LD_LIBRARY_PATH.
run ldconfig

copy bin/wait-for-file .
cmd ./wait-for-file "$DD_TRACE_AGENT_URL" && ./tracer_example
