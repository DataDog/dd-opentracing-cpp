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
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, std::shared_ptr<SpanBuffer>{buffer},
                                              get_time, get_id, KeepAllSampler()}};

    auto span = tracer->StartSpanWithOptions("/should_be_kept", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces.size() == 1);
    auto &result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result->type == "web");
    REQUIRE(result->service == "service_name");
    REQUIRE(result->name == "/should_be_kept");
    REQUIRE(result->resource == "/should_be_kept");
    // This sampler should not set the _sample_rate tag.
    REQUIRE(result->meta["_sample_rate"] == std::string());
  }

  SECTION("discard all tracer") {
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, std::shared_ptr<SpanBuffer>{buffer},
                                              get_time, get_id, DiscardAllSampler()}};

    auto span = tracer->StartSpanWithOptions("/should_be_discarded", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces.size() == 0);
  }

  SECTION("constant rate sampler") {
    double rate = 0.25;
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, std::shared_ptr<SpanBuffer>{buffer},
                                              get_time, get_id, ConstantRateSampler(rate)}};

    for (int i = 0; i < 100; i++) {
      auto span = tracer->StartSpanWithOptions("/constant_rate_sample", span_options);
      const ot::FinishSpanOptions finish_options;
      span->FinishWithOptions(finish_options);
    }

    auto size = buffer->traces.size();
    // allow for a tiny bit of variance. double brackets because of macro
    REQUIRE((size >= 24 && size <= 26));
    auto rate_string = std::to_string(rate);
    std::for_each(buffer->traces.begin(), buffer->traces.end(), [&](auto &trace_iter) {
      auto &span = trace_iter.second.finished_spans->at(0);
      REQUIRE(span->name == "/constant_rate_sample");
      REQUIRE(span->meta["_sample_rate"] == rate_string);
    });
  }

  SECTION("constant rate sampler applied to child spans within same trace") {
    double rate = 1.0;
    std::shared_ptr<ot::Tracer> tracer{new Tracer{tracer_options,
                                                  std::shared_ptr<SpanBuffer>{buffer}, get_time,
                                                  get_id, ConstantRateSampler(rate)}};
    auto ot_root_span = tracer->StartSpan("/constant_rate_sample");
    uint64_t trace_id = (dynamic_cast<const Span *>(ot_root_span.get()))->traceId();
    auto ot_child_span =
        tracer->StartSpan("/child_span", {opentracing::ChildOf(&ot_root_span->context())});

    ot_child_span->Finish();
    ot_root_span->Finish();

    // One trace should have been captured.
    REQUIRE(buffer->traces.size() == 1);

    // Both spans should be recorded under the same trace.
    REQUIRE(buffer->traces[trace_id].finished_spans->size() == 2);

    // The trace id should be the same.
    auto &root_span = buffer->traces[trace_id].finished_spans->at(1);
    auto &child_span = buffer->traces[trace_id].finished_spans->at(0);
    REQUIRE(root_span->traceId() == child_span->traceId());
    // The span id should be different.
    REQUIRE(root_span->spanId() != child_span->spanId());
  }

  SECTION("constant rate sampler applied to child spans from upstream") {
    double rate = 0.25;
    std::shared_ptr<ot::Tracer> tracer{new Tracer{tracer_options,
                                                  std::shared_ptr<SpanBuffer>{buffer}, get_time,
                                                  get_id, ConstantRateSampler(rate)}};
    for (int i = 0; i < 100; i++) {
      // Each trace requires a unique span context (trace id) to represent an extracted context
      // from upstream.
      SpanContext span_context{uint64_t(100 + i), uint64_t(200 + i), {}};
      auto span = tracer->StartSpan("/child_span", {opentracing::ChildOf(&span_context)});
      const ot::FinishSpanOptions finish_options;
      span->FinishWithOptions(finish_options);
    }

    auto size = buffer->traces.size();
    // allow for a tiny bit of variance. double brackets because of macro
    REQUIRE((size >= 24 && size <= 26));
    auto rate_string = std::to_string(rate);
    std::for_each(buffer->traces.begin(), buffer->traces.end(), [&](auto &trace_iter) {
      auto &span = trace_iter.second.finished_spans->at(0);
      REQUIRE(span->name == "/child_span");
      REQUIRE(span->meta["_sample_rate"] == rate_string);
    });
  }
}
