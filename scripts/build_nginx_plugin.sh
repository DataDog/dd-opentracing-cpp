#!/bin/bash
set -eo pipefail

# FIRST: You must have installed the dependencies and built dd-opentracing-cpp:
#     ./scripts/install_dependencies.sh
#     mkdir .build && pushd .build
#     cmake -DBUILD_STATIC=ON ..
#     make && make install
#     popd

NGINX_VERSION=${NGINX_VERSION:-1.14.0}
MODULE_DIR=${MODULE_DIR:-/tmp/build/}

rm -rf .nginx-build
mkdir -p .nginx-build
pushd .nginx-build

wget -O nginx-release-${NGINX_VERSION}.tar.gz https://github.com/nginx/nginx/archive/release-${NGINX_VERSION}.tar.gz
tar zxf nginx-release-$NGINX_VERSION.tar.gz
cd nginx-release-$NGINX_VERSION

# Set up an export map so that symbols from the opentracing module don't
# clash with symbols from other libraries.
cat <<EOF > export.map
{
  global:
    ngx_*;
  local: *;
};
EOF

./auto/configure \
      --with-compat \
      --add-dynamic-module=../../nginx-plugin
make modules

# Statically linking won't work correctly unless g++ is used instead of gcc, and
# there doesn't seem to be any way to have nginx build with g++
# (-with-cc=g++ will fail when compiling the c files), so manually
# redo the linking.
g++-7 -o ngx_http_dd_opentracing_module.so \
  objs/addon/src/*.o \
  objs/ngx_http_dd_opentracing_module_modules.o \
  -static-libstdc++ -static-libgcc \
  -Wl,-static \
  -ldd_opentracing \
  -lopentracing \
  -lcurl -lz \
  -Wl,--version-script="${PWD}/export.map" \
  -shared

# TARGET_NAME=linux-amd64-nginx-${NGINX_VERSION}-ngx_http_module.so.tgz
# tar czf ${TARGET_NAME} ngx_http_dd_opentracing_module.so
cp ngx_http_dd_opentracing_module.so "${MODULE_DIR}"/

popd
