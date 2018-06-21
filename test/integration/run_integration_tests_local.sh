#!/bin/bash
# Runs integration tests locally.
docker-compose --file "${BASH_SOURCE%/*}/nginx/docker-compose.yml" up --build
