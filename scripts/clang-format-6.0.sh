#!/bin/sh

# If clang-format-6.0 is in the PATH, then use that one.
if command -v clang-format-6.0 >/dev/null; then
    exec clang-format-6.0 "$@"
fi

# We don't have clang-format-6.0.  Use a docker image instead.  Build the
# image as needed.
image=dd-opentracing-cpp/clang-format-6.0

>/dev/null docker build -t "$image" - <<END_DOCKERFILE
FROM ubuntu:20.04

RUN apt-get update && apt-get install -y clang-format-6.0

RUN mkdir /workdir
VOLUME /workdir
WORKDIR /workdir

ENTRYPOINT ["clang-format-6.0"]
CMD ["--help"]
END_DOCKERFILE

# Prepare the command line arguments for the container.
#
# Path-like command line arguments must be fully resolved and then prefixed
# with a dot (so that the container can locate them relative to where our file
# system is mounted, which will also be the WORKDIR of the container).
#
# Option-like command line arguments are left alone.
#
# Dealing with lists of arguments, each of which might contain whitespaces, is
# tricky.  The best way in POSIX shell is to use the arguments ($1, $2, ...) of
# a function.
resolve_path_args() {
    while [ $# -ne 0 ]; do
        case "$1" in
          "-"*) echo "$1" ;;
          *)  echo ."$(realpath "$1")" ;;
        esac
        shift
    done
}

resolve_path_args "$@" | xargs docker run \
       --rm \
       --interactive \
       --mount type=bind,source=/,destination=/workdir \
       "$image"
