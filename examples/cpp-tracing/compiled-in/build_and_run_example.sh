#!/bin/bash

if [ -z "$DD_API_KEY" ]
then
  echo "Please set environment variable DD_API_KEY"
  exit 1
fi

DD_API_KEY=${DD_API_KEY} docker-compose up \
  --build \
  --abort-on-container-exit \
  --exit-code-from dd-opentracing-cpp-example
docker rm dd-agent
