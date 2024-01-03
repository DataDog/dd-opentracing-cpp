#!/bin/sh

# This script is a wrapper around "curl".
#
# It passes its command line arguments to "curl," but has the following
# additional behavior:
#
# - The progress meter is suppressed.
# - The first line of standard output is the JSON object produced by the
#   "--write-out ${json}" option
# - The second line of standard input are the response headers represented as a
#   JSON array of key/value arrays, e.g.
#   [["Content-Type", "application/json"], ...].
# - The third line of standard input is the response body represented as
#   a JSON string, i.e. double quoted and with escape sequences.
#
# The intention is that the test driver invoke this script using
# `docker compose exec`.  The output format is chosen to be easy to parse in
# Python.

tmpdir=$(mktemp -d)
body_file="$tmpdir"/json
touch "$body_file"

# Print information about the response as a JSON object on one line.
curl --write-out '%{json}' --output "$body_file" --include --no-progress-meter "$@"
status=$?

# Print the response headers as a JSON array of pairs of strings
# [[key, value], ...]. The jq expression is from
# <https://stackoverflow.com/a/31757661>.
printf '\n'
sed -n 's/^\(.\+\)\r$/\1/p; /^\r$/d' "$body_file" | # just the header, without \r's
    sed 's/^\([^:]*\):\s*\(.*\)$/\1\n\2/' | # "foo: bar" -> foo\nbar
    tail -n +2 | # skip the first line, which is something like "HTTP/1.1 200"
    jq --raw-input --slurp --compact-output \
        'split("\n") | to_entries | group_by(.key/2 | floor) | map(map(.value)) | .[:-1]'

# Skip the header. The sed expression is from
# <https://stackoverflow.com/a/32569573>.
# Print the request body as one quoted JSON string on one line.
sed '1,/^\r$/ d' "$body_file" | jq --raw-input --slurp

rm -r "$tmpdir" >&2
exit $status
