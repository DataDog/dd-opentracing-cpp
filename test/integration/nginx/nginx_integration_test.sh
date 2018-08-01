#!/bin/bash
# Runs nginx integration test.
# Prerequisites: 
#  * nginx and datadog tracing module installed.
#  * Java, Golang 
# Run this test from the Docker container or CircleCI.

# Command to run nginx
if which nginx >/dev/null
then # Running in CI (with nginx from repo)
  service nginx stop
  NGINX='nginx'
else # Running locally/in Dockerfile (with source-compiled nginx)
  NGINX='/usr/local/nginx/sbin/nginx'
fi
function run_nginx() {
  eval "$NGINX -g \"daemon off;\" 1>/tmp/nginx_log.txt &"
  NGINX_PID=$!
  sleep 3 # Wait for nginx to start
}
function kill_nginx() {
  kill $NGINX_PID
  wait $NGINX_PID
}

# TEST 1: Ensure the right traces sent to the agent.

# Get msgpack command-line interface
go get github.com/jakm/msgpack-cli

# Get wiremock
if ! which wiremock >/dev/null
then
  wget  http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar
  printf '#!/bin/bash\nset -x\njava -jar '"$(pwd)/wiremock-standalone-2.18.0.jar \"\$@\"\n" > /usr/local/bin/wiremock && \
  chmod a+x /usr/local/bin/wiremock
fi
# Start wiremock in background
wiremock --port 8126 &
WIREMOCK_PID=$!
# Wait for wiremock to start
sleep 5 
# Set wiremock to respond to trace requests
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "OK" }}' http://localhost:8126/__admin/mappings/new

# Send requests to nginx
run_nginx

curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt

# Read out the traces sent to the agent.
I=0
touch ~/got.json
while ((I++ < 15)) && [[ $(jq 'length' ~/got.json) != "3" ]]
do
  sleep 1
  echo "" > ~/requests.json
  REQUESTS=$(curl -s http://localhost:8126/__admin/requests)
  echo "${REQUESTS}" | jq -r '.requests[].request.bodyAsBase64' | while read line; 
  do 
    echo $line | base64 -d > ~/requests.bin; /root/go/bin/msgpack-cli decode ~/requests.bin | jq . >> ~/requests.json;
  done;
  # Merge 1 or more agent requests back into a single list of traces.
  jq -s 'add' ~/requests.json > ~/got.json
done

# Compare what we got (got.json) to what we expect (expected.json).

# Do a comparison that strips out data that changes (randomly generated ids, times, durations)
STRIP_QUERY='del(.[] | .[] | .start, .duration, .span_id, .trace_id, .parent_id) | del(.[] | .[] | .meta | ."http_user_agent", ."peer.address", ."nginx.worker_pid", ."http.host")'
GOT=$(cat ~/got.json | jq -rS "${STRIP_QUERY}")
EXPECTED=$(cat expected.json | jq -rS "${STRIP_QUERY}")
DIFF=$(diff <(echo "$GOT") <(echo "$EXPECTED"))

if [[ ! -z "${DIFF}" ]]
then
  cat /tmp/curl_log.txt
  echo ""
  echo "Incorrect traces sent to agent"
  echo -e "Got:\n${GOT}\n"
  echo -e "Expected:\n${EXPECTED}\n"
  echo "Diff:"
  echo "${DIFF}"
  exit 1
fi

kill_nginx
pkill -P $WIREMOCK_PID
# TEST 2: Check that libcurl isn't writing to stdout
rm /tmp/nginx_log.txt
run_nginx
curl -s localhost?[1-10000] 1> /dev/null

if [ "$(cat /tmp/nginx_log.txt)" != "" ]
then
  echo "Nginx stdout should be empty, but was:"
  cat /tmp/nginx_log.txt
  echo ""
  exit 1
fi

kill_nginx
