#include "../src/span.h"
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
  int id = 100;                        // Starting span id.
  std::tm start{0, 0, 0, 12, 2, 107};  // Starting calendar time 2007-03-12 00:00:00
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto writer = new MockWriter();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  TracerOptions tracer_options{"", 0, "service_name", "service_name.span_name", "web"};
  std::shared_ptr<Tracer> tracer{
      new Tracer{tracer_options, std::shared_ptr<Writer>{writer}, get_time, get_id}};
  const ot::StartSpanOptions span_options;

  SECTION("names spans correctly") {
    auto span = tracer->StartSpanWithOptions("/what_up", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(writer->spans.size() == 1);
    REQUIRE(writer->spans[0].type == "web");
    REQUIRE(writer->spans[0].service == "service_name");
    REQUIRE(writer->spans[0].name == "service_name.span_name");
    REQUIRE(writer->spans[0].resource == "/what_up");
  }

  SECTION("spans receive id") {
    auto span = tracer->StartSpanWithOptions("", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(writer->spans.size() == 1);
    REQUIRE(writer->spans[0].span_id == 100);
    REQUIRE(writer->spans[0].trace_id == 100);
    REQUIRE(writer->spans[0].parent_id == 0);
  }
}
