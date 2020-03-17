#include "../src/sample.h"
#include "../src/agent_writer.h"
#include "../src/span.h"
#include "../src/tracer.h"
#include "mocks.h"

#include <ctime>

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

/*
TEST_CASE("sample") {
  int id = 100;  // Starting span id.
  // Starting calendar time 2007-03-12 00:00:00
  std::tm start{};
  start.tm_mday = 12;
  start.tm_mon = 2;
  start.tm_year = 107;
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto buffer = std::make_shared<MockBuffer>();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  const ot::StartSpanOptions span_options;

  SECTION("keep all traces") {
    std::shared_ptr<Tracer> tracer{
        new Tracer{tracer_options, buffer, get_time, get_id,
                   std::shared_ptr<SampleProvider>{new KeepAllSampler()}}};

    auto span = tracer->StartSpanWithOptions("/should_be_kept", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces().size() == 1);
    auto &result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->type == "web");
    REQUIRE(result->service == "service_name");
    REQUIRE(result->name == "/should_be_kept");
    REQUIRE(result->resource == "/should_be_kept");
    // This sampler should not set the _sample_rate tag.
    REQUIRE(result->meta["_sample_rate"] == std::string());
  }

  SECTION("discard all tracer") {
    std::shared_ptr<Tracer> tracer{
        new Tracer{tracer_options, buffer, get_time, get_id, std::shared_ptr<SampleProvider>{new
DiscardAllSampler()}}};

    auto span = tracer->StartSpanWithOptions("/should_be_discarded", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    REQUIRE(buffer->traces().size() == 0);
  }

  SECTION("discard rate sampler") {
    double rate = 0.75;
    std::shared_ptr<Tracer> tracer{
        new Tracer{tracer_options, buffer, get_time, get_id,
                   std::shared_ptr<SampleProvider>{new DiscardRateSampler(rate)}}};

    for (int i = 0; i < 100; i++) {
      auto span = tracer->StartSpanWithOptions("/discard_rate_sample", span_options);
      const ot::FinishSpanOptions finish_options;
      span->FinishWithOptions(finish_options);
    }

    auto size = buffer->traces().size();
    // allow for a tiny bit of variance. double brackets because of macro
    REQUIRE((size >= 24 && size <= 26));
  }

  SECTION("discard rate sampler applied to child spans within same trace") {
    double rate = 0;
    std::shared_ptr<ot::Tracer> tracer{
        new Tracer{tracer_options, buffer, get_time, get_id,
                   std::shared_ptr<SampleProvider>{new DiscardRateSampler(rate)}}};
    auto ot_root_span = tracer->StartSpan("/discard_rate_sample");
    uint64_t trace_id = (dynamic_cast<const Span *>(ot_root_span.get()))->traceId();
    auto ot_child_span =
        tracer->StartSpan("/child_span", {opentracing::ChildOf(&ot_root_span->context())});

    ot_child_span->Finish();
    ot_root_span->Finish();

    // One trace should have been captured.
    REQUIRE(buffer->traces().size() == 1);

    // Both spans should be recorded under the same trace.
    REQUIRE(buffer->traces(trace_id).finished_spans->size() == 2);

    // The trace id should be the same.
    auto &root_span = buffer->traces(trace_id).finished_spans->at(1);
    auto &child_span = buffer->traces(trace_id).finished_spans->at(0);
    REQUIRE(root_span->traceId() == child_span->traceId());
    // The span id should be different.
    REQUIRE(root_span->spanId() != child_span->spanId());
  }
}
*/

TEST_CASE("priority sampler unit test") {
  PrioritySampler sampler;
  auto buffer = std::make_shared<MockBuffer>();

  SECTION("default unconfigured priority sampling behaviour is to always sample") {
    auto result = sampler.sample("", "", 0);
    REQUIRE(result.priority_rate == 1.0);
    REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
    result = sampler.sample("env", "service", 1);
    REQUIRE(result.priority_rate == 1.0);
    REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
  }

  SECTION("configured") {
    sampler.configure("{ \"service:nginx,env:\": 0.8, \"service:nginx,env:prod\": 0.2 }"_json);

    SECTION("spans that don't match a rule use the default rate") {
      auto result = sampler.sample("different env", "different service", 1);
      REQUIRE(result.priority_rate == 1.0);
      REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
    }

    SECTION("spans can be sampled") {
      // Case 1, service:nginx,env: => 0.8
      int count_sampled = 0;
      int total = 10000;
      for (int i = 0; i < total; i++) {
        auto result = sampler.sample("", "nginx", getId());
        const auto& p = result.sampling_priority;

        REQUIRE(p != nullptr);
        REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
        count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
      }
      double sample_rate = count_sampled / static_cast<double>(total);
      REQUIRE((sample_rate < 0.85 && sample_rate > 0.75));
      // Case 2, service:nginx,env:prod => 0.2
      count_sampled = 0;
      total = 10000;
      for (int i = 0; i < total; i++) {
        auto result = sampler.sample("", "nginx", getId());
        const auto& p = result.sampling_priority;
        REQUIRE(p != nullptr);
        REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
        count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
      }
      sample_rate = count_sampled / static_cast<double>(total);
      REQUIRE((sample_rate < 0.85 && sample_rate > 0.75));
    }
  }
}

TEST_CASE("rules sampler") {
  auto sampler = std::make_shared<RulesSampler>();
  TracerOptions tracer_options;
  tracer_options.service = "test.service";
  tracer_options.sampling_rules = R"([
    {"name": "test.trace", "service": "test.service", "sample_rate": 0.1},
    {"name": "name.only.match", "sample_rate": 0.2},
    {"service": "service.only.match", "sample_rate": 0.3},
    {"sample_rate": 1.0}
])";
  auto mwriter = std::make_shared<MockWriter>(sampler);
  auto writer = std::shared_ptr<Writer>(mwriter);
  auto tracer = std::make_shared<Tracer>(tracer_options, writer, sampler);

  struct RulesSamplerTestCase {
    std::string service;
    std::string name;
    bool matched;
    double rate;
  };
  auto test_case = GENERATE(values<RulesSamplerTestCase>({
      {"test.service", "test.trace", true, 0.1},
      {"any.service", "name.only.match", true, 0.2},
      {"service.only.match", "any.name", true, 0.3},
      {"any.service", "any.name", true, 1.0},
  }));
  auto result = sampler->match(test_case.service, test_case.name);
  REQUIRE(test_case.matched == result.matched);
  if (std::isnan(test_case.rate)) {
    REQUIRE(std::isnan(result.rate));
  } else {
    REQUIRE(test_case.rate == result.rate);
  }
}
