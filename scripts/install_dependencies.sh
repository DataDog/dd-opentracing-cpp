#!/bin/bash
set -eo pipefail

OPENTRACING_VERSION=${OPENTRACING_VERSION:-1.4.0}
CURL_VERSION=${CURL_VERSION:-7.60.0}
MSGPACK_VERSION=${MSGPACK_VERSION:-3.0.1}

# Allow specifying dependencies not to install. By default we want to compile
# our own versions, but under some circumstances (eg building opentracing-nginx
# docker images) some of these dependencies are already provided.
BUILD_OPENTRACING=1
BUILD_CURL=1
BUILD_MSGPACK=1

while test $# -gt 0
do
  case "$1" in
    not-msgpack) BUILD_MSGPACK=0
      ;;
    not-opentracing) BUILD_OPENTRACING=0
      ;;
    not-curl) BUILD_CURL=0
      ;;
    *) echo "unknown dependency: $1" && exit 1
      ;;
  esac
  shift
done

# Msgpack
if [ "$BUILD_MSGPACK" -eq "1" ]; then
  wget https://github.com/msgpack/msgpack-c/releases/download/cpp-${MSGPACK_VERSION}/msgpack-${MSGPACK_VERSION}.tar.gz -O msgpack.tar.gz
  tar zxvf msgpack.tar.gz
  mkdir msgpack-${MSGPACK_VERSION}/.build
  cd msgpack-${MSGPACK_VERSION}/.build
  cmake ..
  make
  make install
  cd ../..
fi

# OpenTracing
if [ "$BUILD_OPENTRACING" -eq "1" ]; then
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
fi

# Libcurl
if [ "$BUILD_CURL" -eq "1" ]; then
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
              --enable-shared=no \
              --enable-static=yes \
              --with-pic
  make && make install
  cd ..
fi
