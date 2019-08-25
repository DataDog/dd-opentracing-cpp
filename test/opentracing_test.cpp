#include <datadog/opentracing.h>
#include "mocks.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
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
