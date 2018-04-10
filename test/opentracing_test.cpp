#include <datadog/opentracing.h>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
  SECTION("can be created") {
    auto tracer = makeTracer(TracerOptions{});
    REQUIRE(tracer);
  }
}
