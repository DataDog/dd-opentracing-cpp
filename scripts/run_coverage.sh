#!/bin/sh

usage() {
  cat <<'END_USAGE'
Build the library and its unit tests with coverage instrumentation, run the
unit tests, and then generate a report of the source coverage of the tests.

Options:

  --port=<PORT>
    After generating the coverage report, run an HTTP server on the specified
    <PORT> that serves the report as HTML.

  --help
  -h
    Print this message.
END_USAGE
}

# Exit if any non-conditional command returns a nonzero exit status.
set -e

is_integer() {
  # Any integer is equal to itself, but it's an error if it's not an integer.
  # Ignore the error message and just use the status code.
  [ "$1" -eq "$1" ] 2>/dev/null
}

# If `serve_port` is nonzero, then at the end of this script run an HTTP
# server, listening on the specified port, serving the coverage report.
serve_port=0

# Parse command line options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help|-h)
      usage
      exit ;;
    --port=*)
      serve_port=${1#--port=}
      if ! is_integer "$serve_port" || [ "$serve_port" -le 0 ]; then
        >&2 printf "Invalid port number: %s\n" "$serve_port"
        exit 1
      fi ;;
    *)
      >&2 usage
      >&2 printf "\nUnknown option: %s\n" "$1"
      exit 2
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
rm -rf coverage
mkdir -p coverage
lcov --capture --directory "$BUILD_DIR" --output-file coverage/raw.info
lcov --remove coverage/raw.info -o coverage/filtered.info '/usr/include/*' "$(pwd)/3rd_party/*" "$(pwd)/deps/*" "$(pwd)/test/*"
genhtml coverage/filtered.info --output-directory coverage/report

if [ "$serve_port" -ne 0 ]; then
  python3 -m http.server --directory coverage/report "$serve_port"
fi
