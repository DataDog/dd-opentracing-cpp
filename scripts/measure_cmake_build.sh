#!/bin/sh

# Required utilities (aside from GNU coreutils):
# - cmake
# - git
# - gpg
# - python3
# - realpath

set -e # Exit if a non-conditional command returns a nonzero exit status.

usage() {
    cat <<END_USAGE
measure_cmake_build.sh - Perform an instrumented cmake build and send measurements to a server

usage:

    $0 [ CMAKE_ARGS ] ...
        Perform an instrumented cmake build, passing the optionally specified
        CMAKE_ARGS to cmake.
        
        If the build completes successfully, send the collected compilation
        metrics to the server specified in the BUILD_METRICS_SERVER
        environment variable.

        Use the gpg private key in the specified BUILD_METRICS_GPG_SECRET_KEY
        environment variable to sign the compilation metrics before sending.

        Use the passphrase in the specified
        BUILD_METRICS_GPG_SECRET_KEY_PASSPHRASE environment variable
        to unlock the private key for use in signing.

    $0 --help
    $0 -h
        Print this message.
END_USAGE
}

is_help_flag() {
    [ "$1" = '-h' ] || [ "$1" = '--help' ]
}

if [ $# -eq 1 ] && is_help_flag "$1"; then
    usage
    exit
fi

if [ -z "$BUILD_METRICS_SERVER" ]; then
    >&2 echo 'BUILD_METRICS_SERVER environment variable is required (<host>:<port>).'
    exit 1
elif [ -z "$BUILD_METRICS_GPG_SECRET_KEY" ]; then
    >&2 echo 'BUILD_METRICS_GPG_SECRET_KEY environment variable is required.'
    >&2 echo 'It must be private key block as produced by: gpg --export-secret-keys --armor'
    exit 2
elif [ -z "$BUILD_METRICS_GPG_SECRET_KEY_PASSPHRASE" ]; then
    >&2 echo 'BUILD_METRICS_GPG_SECRET_KEY_PASSPHRASE environment variable is required.'
    >&2 echo 'It is the passphrase used to unlock the secret key for signing.'
    exit 3
fi

cd "$(git rev-parse --show-toplevel)"

# Build the compilation metrics compiler wrapper.
tmpdir=$(mktemp -d)
git clone https://github.com/dgoffredo/compilation-metrics "$tmpdir"/compilation-metrics
make "--directory=$tmpdir/compilation-metrics"

mkdir -p .build
cd .build
cmake \
    "-DCMAKE_CXX_COMPILER_LAUNCHER=$tmpdir/compilation-metrics/wrap_compiler" \
    "$@" \
    ..

COMPILATION_METRICS_DB=$(realpath metrics.db)
export COMPILATION_METRICS_DB
make

rm -rf "$tmpdir"

# Sign `metrics.db` and send it to the server.

# First, import the secret key.
echo "$BUILD_METRICS_GPG_SECRET_KEY" | gpg --batch --armor --import -

# Find the ID of the key that we just imported.
keyid=$(echo "$BUILD_METRICS_GPG_SECRET_KEY" | \
    gpg --batch --armor --import --import-options show-only --with-colons - | \
    awk -F : '/^fpr/ { print $10; exit }')

# Produce a signature for `metrics.db` (`metrics.db.asc`).
gpg \
    --batch \
    --detach-sign \
    --yes \
    --passphrase "$BUILD_METRICS_GPG_SECRET_KEY_PASSPHRASE" \
    --pinentry-mode loopback \
    --armor \
    --default-key "$keyid" \
    metrics.db

# Send the metrics database and its signature to the server.
python3 <<END_PYTHON
import base64
import json
import os
from pathlib import Path
import socket

db_base64 = base64.b64encode(Path('metrics.db').read_bytes()).decode('utf8')
db_signature = Path('metrics.db.asc').read_text()

host, port = os.environ['BUILD_METRICS_SERVER'].split(':')
port = int(port)
with socket.socket() as server:
    server.connect((host, port))
    server.send(json.dumps({
        'database_base64': db_base64,
        'database_signature_ascii': db_signature 
        }).encode('utf8') + b'\n')
END_PYTHON
