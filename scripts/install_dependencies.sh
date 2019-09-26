#!/bin/bash
set -e
install_dir=$(mkdir -p "${0%/*}/../deps" && cd "${0%/*}/../deps" && echo "$PWD")
if [[ ! -d "$install_dir" ]]; then
	echo "Unable to determine install directory"
	exit 1
fi

OPENTRACING_VERSION=${OPENTRACING_VERSION:-1.5.1}
CURL_VERSION=${CURL_VERSION:-7.66.0}
MSGPACK_VERSION=${MSGPACK_VERSION:-3.2.0}
ZLIB_VERSION=${ZLIB_VERSION:-1.2.11}

# Just report versions and exit.
if [[ "$1" == "versions" ]]; then
	echo "opentracing:$OPENTRACING_VERSION"
	echo "curl:$CURL_VERSION"
	echo "msgpack:$MSGPACK_VERSION"
	echo "zlib:$ZLIB_VERSION"
	exit 0
fi

# Allow specifying dependencies not to install. By default we want to compile
# our own versions, but under some circumstances (eg building opentracing-nginx
# docker images) some of these dependencies are already provided.
BUILD_OPENTRACING=1
BUILD_CURL=1
BUILD_MSGPACK=1
BUILD_ZLIB=1

while test $# -gt 0
do
  case "$1" in
    not-msgpack) BUILD_MSGPACK=0
      ;;
    not-opentracing) BUILD_OPENTRACING=0
      ;;
    not-curl) BUILD_CURL=0
      ;;
    not-zlib) BUILD_ZLIB=0
      ;;
    *) echo "unknown dependency: $1" && exit 1
      ;;
  esac
  shift
done

# OpenTracing
if [ "$BUILD_OPENTRACING" -eq "1" ]; then
  wget "https://github.com/opentracing/opentracing-cpp/archive/v${OPENTRACING_VERSION}.tar.gz" -O opentracing-cpp.tar.gz
  tar zxf opentracing-cpp.tar.gz
  mkdir -p "opentracing-cpp-${OPENTRACING_VERSION}/.build"
  cd "opentracing-cpp-${OPENTRACING_VERSION}/.build"
  cmake -DCMAKE_INSTALL_PREFIX="$install_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-fPIC" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_MOCKTRACER=OFF \
        ..
  make
  make install
  cd ../..
  rm -r "opentracing-cpp-${OPENTRACING_VERSION}/"
  rm opentracing-cpp.tar.gz
fi

# Zlib
if [ "$BUILD_ZLIB" -eq "1" ]; then
  wget "https://zlib.net/zlib-${ZLIB_VERSION}.tar.gz"
  tar zxf "zlib-${ZLIB_VERSION}.tar.gz"
  mkdir -p "zlib-${ZLIB_VERSION}"
  cd "zlib-${ZLIB_VERSION}"
  CFLAGS="$CFLAGS -fPIC" ./configure --prefix="$install_dir" --static
  make && make install
  cd ..
  rm -r "zlib-${ZLIB_VERSION}"
  rm "zlib-${ZLIB_VERSION}.tar.gz"
fi

# Msgpack
if [ "$BUILD_MSGPACK" -eq "1" ]; then
  wget "https://github.com/msgpack/msgpack-c/releases/download/cpp-${MSGPACK_VERSION}/msgpack-${MSGPACK_VERSION}.tar.gz" -O msgpack.tar.gz
  tar zxf msgpack.tar.gz
  mkdir -p "msgpack-${MSGPACK_VERSION}/.build"
  cd "msgpack-${MSGPACK_VERSION}/.build"
  cmake -DCMAKE_INSTALL_PREFIX="$install_dir" -DBUILD_SHARED_LIBS=OFF ..
  make
  make install
  cd ../..
  rm -r "msgpack-${MSGPACK_VERSION}/"
  rm msgpack.tar.gz
fi

# Libcurl
if [ "$BUILD_CURL" -eq "1" ]; then
  wget "https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz"
  tar zxf "curl-${CURL_VERSION}.tar.gz"
  mkdir -p "curl-${CURL_VERSION}"
  cd "curl-${CURL_VERSION}"
  ./configure --prefix="$install_dir" \
              --disable-ftp \
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
              --with-zlib \
              --disable-rtsp \
              --enable-shared=no \
              --enable-static=yes \
              --with-pic
  make && make install
  cd ..
  rm -r "curl-${CURL_VERSION}/"
  rm "curl-${CURL_VERSION}.tar.gz"
fi
