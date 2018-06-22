#!/bin/bash
# Runs nginx integration test.
# Prerequisites: 
#  * nginx and datadog tracing module installed.
#  * Java, Golang 
# Run this test from the Docker container or CircleCI.

# Get msgpack command-line interface
go get github.com/jakm/msgpack-cli

# Get wiremock
if ! which wiremock >/dev/null
then
  wget  http://repo1.maven.org/maven2/com/github/tomakehurst/wiremock-standalone/2.18.0/wiremock-standalone-2.18.0.jar
  printf "#!/bin/bash\nset -x\njava -jar $(pwd)/wiremock-standalone-2.18.0.jar \"\$@\"\n" > /usr/local/bin/wiremock && \
  chmod a+x /usr/local/bin/wiremock
fi
# Start wiremock in background
wiremock --port 8129 &
# Wait for wiremock to start
sleep 5 
# Set wiremock to respond to trace requests
curl -s -X POST --data '{ "priority":10, "request": { "method": "ANY", "urlPattern": ".*" }, "response": { "status": 200, "body": "OK" }}' http://localhost:8129/__admin/mappings/new

if which nginx >/dev/null
then # Running in CI (with nginx from repo)
  service nginx stop
  NGINX='nginx'
else # Running locally/in Dockerfile (with source-compiled nginx)
  NGINX='/usr/local/nginx/sbin/nginx'
fi
RUN_NGINX="
  sleep 3 # Wait for previously killed nginx to stop
  $NGINX -g \"daemon off;\" 1>/tmp/nginx_log.txt &
  sleep 3 # Wait for nginx to start
  NGINX_PID=\$!"

# Send requests to nginx
eval "$RUN_NGINX"

curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt
curl -s localhost 1> /tmp/curl_log.txt

# Read out the traces sent to the agent.
I=0
while ((I++ < 15)) && [[ -z "${REQUESTS}" || $(echo "${REQUESTS}" | jq -r '.requests | length') == "0" ]]
do
  sleep 1
  REQUESTS=$(curl -s http://localhost:8129/__admin/requests)
done

pkill java # Exit wiremock
echo "${REQUESTS}" | jq -r '.requests[0].request.bodyAsBase64' | base64 -d > ~/requests.bin
/root/go/bin/msgpack-cli decode ~/requests.bin --pp > ~/got.json

# Compare what we got (got.json) to what we expect (expected.json).

# Do a comparison that strips out data that changes (randomly generated ids, times, durations)
STRIP_QUERY='del(.[] | .[] | .start, .duration, .span_id, .trace_id, .parent_id) | del(.[] | .[] | .meta | ."peer.address", ."nginx.worker_pid", ."http.host")'
GOT=$(cat ~/got.json | jq -rS "${STRIP_QUERY}")
EXPECTED=$(cat expected.json | jq -rS "${STRIP_QUERY}")
DIFF=$(diff <(echo "$GOT") <(echo "$EXPECTED"))

if [[ ! -z "${DIFF}" ]]
then
  cat /tmp/curl_log.txt
  echo "Incorrect traces sent to agent"
  echo -e "Got:\n${GOT}\n"
  echo -e "Expected:\n${EXPECTED}\n"
  echo "Diff:"
  echo "${DIFF}"
  exit 1
fi

# Check that libcurl isn't writing to stdout
kill $NGINX_PID
rm /tmp/nginx_log.txt
eval "$RUN_NGINX"
curl -s localhost?[1-10000] 1> /dev/null
kill $NGINX_PID

if [ "$(cat /tmp/nginx_log.txt)" != "" ]
then
  echo "Nginx stdout should be empty, but was:"
  cat /tmp/nginx_log.txt
  echo ""
  exit 1
fi
