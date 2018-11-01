#!/bin/bash

if [ -z "$DD_API_KEY" ]
then
  echo "Please set environment variable DD_API_KEY"
  exit 1
fi

# Build the example.
docker build -t dd-opentracing-cpp-example .
# Start the agent.
# AGENT_CONTAINER_ID=$(docker run -d --name dd-agent -v /var/run/docker.sock:/var/run/docker.sock:ro -v /proc/:/host/proc/:ro -v /sys/fs/cgroup/:/host/sys/fs/cgroup:ro -e DD_API_KEY=${DD_API_KEY} datadog/agent:latest)
# Run the example.
docker run -t dd-opentracing-cpp-example

sleep 20 # Wait for agent to send traces
# docker kill ${AGENT_CONTAINER_ID}
# docker rm ${AGENT_CONTAINER_ID}

echo "Check for your traces in https://app.datadoghq.com/apm/services"
