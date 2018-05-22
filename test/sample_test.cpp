#include "../src/sample.h"
#include "../src/span.h"
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("sample") {
  int id = 100;                        // Starting span id.
  std::tm start{0, 0, 0, 12, 2, 107};  // Starting calendar time 2007-03-12 00:00:00
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto buffer = new MockBuffer();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  const ot::StartSpanOptions span_options;

  SECTION("keep all traces") {
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options,
                                              std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time,
                                              get_id, KeepAllSampler()}};

    auto span = tracer->StartSpanWithOptions("/should_be_kept", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces.size() == 1);
    auto result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result.type == "web");
    REQUIRE(result.service == "service_name");
    REQUIRE(result.name == "/should_be_kept");
    REQUIRE(result.resource == "/should_be_kept");
    // This sampler should not set the _sample_rate tag.
    REQUIRE(result.meta["_sample_rate"] == std::string());
  }

  SECTION("discard all tracer") {
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options,
                                              std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time,
                                              get_id, DiscardAllSampler()}};

    auto span = tracer->StartSpanWithOptions("/should_be_discarded", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces.size() == 0);
  }

  SECTION("constant rate sampler") {
    double rate = 0.5;
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options,
                                              std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time,
                                              get_id, ConstantRateSampler(rate)}};

    for (int i = 0; i < 100; i++) {
      auto span = tracer->StartSpanWithOptions("/constant_rate_sample", span_options);
      const ot::FinishSpanOptions finish_options;
      span->FinishWithOptions(finish_options);
    }

    auto size = buffer->traces.size();
    // allow for a tiny bit of variance. double brackets because of macro
    REQUIRE((size >= 49 && size <= 51));
    auto rate_string = std::to_string(rate);
    std::for_each(buffer->traces.begin(), buffer->traces.end(), [&](auto &trace_iter) {
      auto span = trace_iter.second.finished_spans->at(0);
      REQUIRE(span.name == "/constant_rate_sample");
      REQUIRE(span.meta["_sample_rate"] == rate_string);
    });
  }
}
