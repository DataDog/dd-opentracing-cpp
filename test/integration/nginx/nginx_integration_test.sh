#!/bin/bash
# Runs nginx integration test.
# Prerequisites: 
#  * nginx and datadog tracing module installed.
#  * Java, Golang 
# Run this test from the Docker container or CircleCI.

# Disable tracer startup logs for test purposes
export DD_TRACE_STARTUP_LOGS=false

NGINX_CONF_PATH=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--conf-path=\([^ ]*\).*/\1/p')
NGINX_CONF=$(cat ${NGINX_CONF_PATH})
TRACER_CONF_PATH=/etc/dd-config.json
TRACER_CONF=$(cat ${TRACER_CONF_PATH})
NGINX_ERROR_LOG=$(nginx -V 2>&1 | grep "configure arguments" | sed -n 's/.*--error-log-path=\([^ ]*\).*/\1/p')

function reset_test() {
  kill $NGINX_PID
  wait $NGINX_PID
  pkill -x java # Kill wiremock
  echo ${TRACER_CONF} > ${TRACER_CONF_PATH}
  echo "" > /tmp/curl_log.txt
  echo "" > /tmp/nginx_log.txt
  echo "" > ${NGINX_ERROR_LOG}
}

function get_n_traces() {
  # Read out the traces sent to the agent.
  NUM_TRACES_EXPECTED=${1:0}
  I=0
  echo "" > ~/got.json
  while ((I++ < 15)) && [[ $(jq 'length' ~/got.json) != "${NUM_TRACES_EXPECTED}" ]]
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

  # Strip out data that changes (randomly generated ids, times, durations)
  STRIP_QUERY='del(.[] | .[] | .start, .duration, .span_id, .trace_id, .parent_id) | del(.[] | .[] | .meta | ."http_user_agent", ."peer.address", ."nginx.worker_pid", ."http.host")'
  cat ~/got.json | jq -rS "${STRIP_QUERY}"
  # Reset request log.
  curl -X POST -s http://localhost:8126/__admin/requests/reset > /dev/null
}

function wait_for_port() {
  if [ "$1" == "" ]; then
    return
  fi

  for ((i=0; i<60; i++)); do
    # Check at 0.25s intervals
    sleep 0.25
    output=$(ss -ntlp sport eq :"$1" | tail -n +2)
    if [ -n "$output" ]; then
      # It's listening now.
      return 0
    fi
  done
  # Return an error if it never showed up as listening
  return 1
}

function run_nginx() {
  eval "nginx -g \"daemon off;\" 1>/tmp/nginx_log.txt &"
  NGINX_PID=$!
  wait_for_port 80
}

# TEST 1: Ensure the right traces sent to the agent.
# Start wiremock in background
wiremock --port 8126 >/dev/null 2>&1 &
# Wait for wiremock to start
wait_for_port 8126
# Set wiremock to respond to trace requests
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "OK" }}' http://localhost:8126/__admin/mappings/new

# Send requests to nginx
run_nginx

curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt

GOT=$(get_n_traces 3)
EXPECTED=$(cat expected_tc1.json | jq -rS "${STRIP_QUERY}")
DIFF=$(diff -u <(echo "$GOT") <(echo "$EXPECTED"))

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

reset_test
# TEST 2: Check that libcurl isn't writing to stdout
run_nginx
curl -s localhost?[1-10000] 1> /tmp/curl_log.txt

if [ "$(cat /tmp/nginx_log.txt)" != "" ]
then
  echo "Nginx stdout should be empty, but was:"
  cat /tmp/nginx_log.txt
  echo ""
  exit 1
fi

reset_test
# TEST 3: Check that creating a root span doesn't produce an error
run_nginx
curl -s localhost?[1-5] 1> /tmp/curl_log.txt

if [ "$(cat ${NGINX_ERROR_LOG} | grep 'failed to extract an opentracing span context' | wc -l)" != "0" ]
then
  echo "Extraction errors in nginx log file:"
  cat ${NGINX_ERROR_LOG}
  echo ""
  exit 1
elif [ "$(cat ${NGINX_ERROR_LOG})" != "" ]
then
  echo "Other errors in nginx log file:"
  cat ${NGINX_ERROR_LOG}
  echo ""
  exit 1
fi

reset_test
# Test 4: Ensure that NGINX errors are reported to Datadog
wiremock --port 8126 >/dev/null 2>&1 &
# Wait for wiremock to start
wait_for_port 8126
# Set wiremock to respond to trace requests
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "OK" }}' http://localhost:8126/__admin/mappings/new
# Start a proxied server to receive distributed traces.
wiremock --port 8080 >/dev/null 2>&1 & wait_for_port 8080
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 500, "body": "This is the sad face" }}' http://localhost:8080/__admin/mappings/new
run_nginx

curl -s localhost/get_error/ 1> /tmp/curl_log.txt

GOT=$(get_n_traces 1)
ERROR=$(echo $GOT | jq '.[] | .[] | .error')

if ! [ "$ERROR" = "1" ]
then
  echo "Error field not set on trace"
  exit 1
fi

reset_test

# Test 5: Origin header is propagated and adds a tag
wiremock --port 8126 >/dev/null 2>&1 & wait_for_port 8126
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "OK" }}' http://localhost:8126/__admin/mappings/new
wiremock --port 8080 >/dev/null 2>&1 & wait_for_port 8080
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "Hello World" }}' http://localhost:8080/__admin/mappings/new

run_nginx

curl_flags=(
  -H "x-datadog-trace-id: 123"
  -H "x-datadog-parent-id: 123"
  -H "x-datadog-sampling-priority: 1"
  -H "x-datadog-origin: synthetics"
)
curl -s "${curl_flags[@]}" localhost/proxy/?1 1> /tmp/curl_log.txt
ORIGIN_HEADER=$(curl -s http://localhost:8080/__admin/requests | jq -r '.requests[].request.headers."x-datadog-origin" == "synthetics"')
if [ $ORIGIN_HEADER != "true" ]; then
  echo "Origin header not propagated"
  exit 1
fi
GOT=$(get_n_traces 1)
EXPECTED=$(cat expected_tc6.json | jq -rS "${STRIP_QUERY}")
DIFF=$(diff -u <(echo "$GOT") <(echo "$EXPECTED"))

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
