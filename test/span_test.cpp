#include "../src/span.h"
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("span") {
  int id = 100;                        // Starting span id.
  std::tm start{0, 0, 0, 12, 2, 107};  // Starting calendar time 2007-03-12 00:00:00
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto recorder = new MockRecorder();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  const ot::StartSpanOptions span_options;

  SECTION("receives id") {
    Span span{nullptr,     std::shared_ptr<Recorder>{recorder}, get_time, get_id, "", "", "", "",
              span_options};
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    REQUIRE(recorder->spans.size() == 1);
    REQUIRE(recorder->spans[0].span_id == 100);
    REQUIRE(recorder->spans[0].trace_id == 100);
    REQUIRE(recorder->spans[0].parent_id == 0);
  }

  SECTION("timed correctly") {
    Span span{nullptr,     std::shared_ptr<Recorder>{recorder}, get_time, get_id, "", "", "", "",
              span_options};
    advanceSeconds(time, 10);
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    REQUIRE(recorder->spans.size() == 1);
    REQUIRE(recorder->spans[0].duration == 10000000000);
  }
}
