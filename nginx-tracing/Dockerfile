# Builds and runs a simple nginx server, traced by Datadog
FROM ubuntu

RUN apt-get update
RUN apt-get install -y git build-essential libpcre3-dev zlib1g-dev libcurl4-openssl-dev wget curl tar cmake

# Install opentracing dependency
RUN wget -O opentracing-cpp.tar.gz https://github.com/opentracing/opentracing-cpp/archive/v1.3.0.tar.gz
RUN tar zxvf opentracing-cpp.tar.gz
RUN mkdir opentracing-cpp-1.3.0/.build
WORKDIR opentracing-cpp-1.3.0/.build
RUN cmake ..
RUN make
RUN make install
WORKDIR ../..

# Install msgpack dependency
RUN wget https://github.com/msgpack/msgpack-c/releases/download/cpp-2.1.5/msgpack-2.1.5.tar.gz
RUN tar zxvf msgpack-2.1.5.tar.gz
RUN mkdir msgpack-2.1.5/.build
WORKDIR msgpack-2.1.5/.build
RUN cmake ..
RUN make
RUN make install
WORKDIR ../..

# Compile dd-opentracing-cpp
ADD ./ ./dd-opentracing-cpp/
RUN rm -rf dd-opentracing-cpp/.build 
RUN mkdir dd-opentracing-cpp/.build
WORKDIR dd-opentracing-cpp/.build
RUN cmake ..
RUN make
RUN make install
# Warning: Sanitizers (tsan, usan, msan, asan) are not run on these tests, see the CircleCI
# config.yml for an example of running tests with those checks.
RUN ctest --output-on-failure
WORKDIR ../..

# Fetch and patch the nginx-opentracing module
RUN wget -O nginx-opentracing.tar.gz https://github.com/opentracing-contrib/nginx-opentracing/archive/v0.2.1.tar.gz
RUN tar zxvf nginx-opentracing.tar.gz
WORKDIR nginx-opentracing-0.2.1
RUN patch -p1 < ../dd-opentracing-cpp/nginx-opentracing-datadog.patch
WORKDIR ..

# Build nginx
RUN wget http://nginx.org/download/nginx-1.13.10.tar.gz
RUN tar zxvf nginx-1.13.10.tar.gz
WORKDIR nginx-1.13.10
RUN ./configure \
    --add-dynamic-module=../nginx-opentracing-0.2.1/opentracing \
    --add-dynamic-module=../nginx-opentracing-0.2.1/datadog
RUN make
RUN make install
WORKDIR ..

RUN ldconfig

ADD ./nginx-tracing/nginx.conf /usr/local/nginx/conf/nginx.conf
RUN /usr/local/nginx/sbin/nginx -t

EXPOSE 80
CMD [ "/usr/local/nginx/sbin/nginx", "-g", "daemon off;"]
