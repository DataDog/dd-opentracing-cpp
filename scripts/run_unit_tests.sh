#!/bin/sh

# Exit if any non-conditional command returns a nonzero exit status.
set -e

# Get to the root of the repository.
cd "$(dirname "$0")"
cd "$(git rev-parse --show-toplevel)"

# Default build directory is ".build", but you might prefer another (e.g.
# VSCode uses "build").
BUILD_DIR="${BUILD_DIR:-.build}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DBUILD_TESTING=ON ..
make --jobs=$(nproc)
ctest --output-on-failure
