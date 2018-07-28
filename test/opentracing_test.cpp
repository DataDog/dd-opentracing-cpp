#include <datadog/opentracing.h>
#include "mocks.h"

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
  SECTION("can be created") {
    auto tracer = makeTracer(TracerOptions{});
    REQUIRE(tracer);
  }
  SECTION("can be created with external Writer implementation") {
    std::shared_ptr<TracePublisher> publisher;
    auto tracer = makeTracer(TracerOptions{}, publisher);
    REQUIRE(tracer);
    REQUIRE(publisher);
  }
}
