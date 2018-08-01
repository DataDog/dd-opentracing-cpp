#!/bin/bash
set -eo pipefail

# OpenTracing
wget https://github.com/opentracing/opentracing-cpp/archive/v${OPENTRACING_VERSION}.tar.gz -O opentracing-cpp.tar.gz
tar zxvf opentracing-cpp.tar.gz
mkdir opentracing-cpp-${OPENTRACING_VERSION}/.build
cd opentracing-cpp-${OPENTRACING_VERSION}/.build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-fPIC" \
      -DBUILD_SHARED_LIBS=OFF \
      -DBUILD_TESTING=OFF \
      -DBUILD_MOCKTRACER=OFF \
      ..
make
make install
cd ../..

# Msgpack
wget https://github.com/msgpack/msgpack-c/releases/download/cpp-${MSGPACK_VERSION}/msgpack-${MSGPACK_VERSION}.tar.gz -O msgpack.tar.gz
tar zxvf msgpack.tar.gz
mkdir msgpack-${MSGPACK_VERSION}/.build
cd msgpack-${MSGPACK_VERSION}/.build
cmake ..
make
make install
cd ../..

# Libcurl
wget https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz
tar zxf curl-${CURL_VERSION}.tar.gz
cd curl-${CURL_VERSION}
./configure --disable-ftp \
            --disable-ldap \
            --disable-dict \
            --disable-telnet \
            --disable-tftp \
            --disable-pop3 \
            --disable-smtp \
            --disable-gopher \
            --without-ssl \
            --disable-crypto-auth \
            --without-axtls \
            --disable-rtsp \
            --enable-shared=yes \
            --enable-static=yes \
            --with-pic
make && make install
cd ..
