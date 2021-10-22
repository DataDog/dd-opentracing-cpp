#!/bin/sh

# Exit if any non-conditional command returns a nonzero exit status.
set -e

# If `serve_port` is nonzero, then at the end of this script run an HTTP
# server, listening on the specified port, serving the coverage report.
serve_port=0

# Parse command line options.
while [ $# -gt 0 ]; do
  case "$1" in
    --serve) serve_port=8000 ;;
    --port=*) serve_port=${1#--port=} ;;
    *) >&2 printf "Unknown option: %s\n" "$1"
  esac
  shift
done

# Get to the root of the repository.
cd "$(dirname "$0")"
cd "$(git rev-parse --show-toplevel)"

# Default build directory is ".build", but you might prefer another (e.g.
# VSCode uses "build").
BUILD_DIR="${BUILD_DIR:-.build}"

# Build and run the unit tests with coverage instrumentation.
scripts/run_unit_tests.sh --coverage

# Generate a coverage report, remove irrelevant source files from it, and then
# render the report as a static website.
mkdir -p coverage
lcov --capture --directory "$BUILD_DIR" --output-file coverage/raw.info
lcov --remove coverage/raw.info -o coverage/filtered.info '/usr/include/*' "$(pwd)/3rd_party/*" "$(pwd)/deps/*" "$(pwd)/test/*"
genhtml coverage/filtered.info --output-directory coverage/report

if [ "$serve_port" -ne 0 ]; then
  python3 -m http.server --directory coverage/report "$serve_port"
fi
