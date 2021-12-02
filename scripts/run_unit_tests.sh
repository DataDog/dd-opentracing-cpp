#!/bin/bash

usage() {
  cat <<'END_USAGE'
Build and run the unit tests.

Options:

  --coverage
    Build the code and tests with instrumentation for tracking source code
    coverage.

  --verbose
    Emit verbose output.  Pass the VERBOSE=1 option to `make` when building,
    and pass the `--verbose` flag to `ctest` when running the tests.

  --help
  -h
    Print this message.
END_USAGE
}

# Exit if any non-conditional command returns a nonzero exit status.
set -e

cmake_flags=('-DBUILD_TESTING=ON')
make_flags=("--jobs=${MAKE_JOB_COUNT:-$(nproc)}")
ctest_flags=('--output-on-failure')

# Parse command line options.
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit ;;
    --coverage)
      cmake_flags+=('-DBUILD_COVERAGE=ON') ;;
    --verbose)
      make_flags+=('VERBOSE=1')
      ctest_flags+=('--verbose') ;;
    *)
      >&2 usage 
      >&2 printf "\nUnknown option: %s\n" "$1"
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
cmake "${cmake_flags[@]}" ..
make "${make_flags[@]}"
ctest "${ctest_flags[@]}"
