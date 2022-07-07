#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

usage() {
    argv0="$0"
    
    cat <<END_USAGE
format.sh - format Datadog C++ source code

usage:

    $argv0 FILES ...
        Format the specified files in place.

    $argv0
        Format all Datadog-owned C++ code in this repository.

    $argv0 -h
    $argv0 --help
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

formatter=clang-format-9

if [ $# -eq 0 ]; then
    cd "$(git rev-parse --show-toplevel)"
    find src/ include/ test/ examples/ fuzz/ \
        -type f \( -name '*.h' -o -name '*.cpp' \) -print0 \
    | xargs -0 "$formatter" -i --style=file
else
    exec "$formatter" -i --style=file "$@"
fi
