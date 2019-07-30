#!/bin/bash

sleep 5

echo "Tracer example"
./tracer_example

echo "Tracer example with std::string tag"
./tracer_example_string

echo "Tracer example with const char*"
./tracer_example_char

sleep 25
