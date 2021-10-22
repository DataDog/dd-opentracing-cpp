#!/bin/sh

# Exit if any non-conditional command returns a nonzero exit status.
set -e

cmake_flags='-DBUILD_TESTING=ON'
make_flags="--jobs=$(nproc)"
ctest_flags='--output-on-failure'

# Parse command line options.
while [ $# -gt 0 ]; do
  case "$1" in
    --coverage) cmake_flags="$cmake_flags -DBUILD_COVERAGE=ON" ;;
    --make-verbose) make_flags="$make_flags VERBOSE=1" ;;
    --ctest-*) ctest_flags="$ctest_flags --${1#--ctest-}" ;;
    *) >&2 printf "Unknown option: %s\n" "$1"
      exit 1
  esac
  shift
done

# Get to the root of the repository.
cd "$(dirname "$0")"
cd "$(git rev-parse --show-toplevel)"

# Default build directory is ".build", but you might prefer another (e.g.
# VSCode uses "build").
BUILD_DIR="${BUILD_DIR:-.build}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake $cmake_flags ..
make $make_flags
ctest $ctest_flags
