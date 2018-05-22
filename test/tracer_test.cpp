#include "../src/sample.h"
#include "../src/span.h"
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
  int id = 100;                        // Starting span id.
  std::tm start{0, 0, 0, 12, 2, 107};  // Starting calendar time 2007-03-12 00:00:00
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto buffer = new MockBuffer();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  SampleProvider sampler = KeepAllSampler();
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{
      tracer_options, std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, sampler}};
  const ot::StartSpanOptions span_options;

  SECTION("names spans correctly") {
    auto span = tracer->StartSpanWithOptions("/what_up", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result.type == "web");
    REQUIRE(result.service == "service_name");
    REQUIRE(result.name == "/what_up");
    REQUIRE(result.resource == "/what_up");
  }

  SECTION("spans receive id") {
    auto span = tracer->StartSpanWithOptions("", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result.span_id == 100);
    REQUIRE(result.trace_id == 100);
    REQUIRE(result.parent_id == 0);
  }
}
