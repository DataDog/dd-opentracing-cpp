#include <datadog/version.h>

#include <iostream>

int main() {
  std::cout << datadog::version::tracer_version << std::endl;
  return 0;
}
