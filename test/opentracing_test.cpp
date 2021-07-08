#include <datadog/opentracing.h>

#include "catch.h"
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
