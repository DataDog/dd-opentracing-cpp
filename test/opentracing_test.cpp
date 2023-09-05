#include <datadog/opentracing.h>

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_all.hpp>

#include "mocks.h"
using namespace datadog::opentracing;

TEST_CASE("opentracing tracer") {
  SECTION("can be created") {
    auto tracer = makeTracer(TracerOptions{});
    REQUIRE(tracer);
  }
  SECTION("can be created with external Writer implementation") {
    auto tp = makeTracerAndEncoder(TracerOptions{});
    auto tracer = std::get<0>(tp);
    auto encoder = std::get<1>(tp);
    REQUIRE(tracer);
    REQUIRE(encoder);
  }
}
