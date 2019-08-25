#include "../src/sample.h"
#include "../src/agent_writer.h"
#include "../src/span.h"
#include "../src/tracer.h"
#include "mocks.h"

#include <ctime>

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

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
        new Tracer{tracer_options, buffer, get_time, get_id,
                   std::shared_ptr<SampleProvider>{new DiscardAllSampler()}}};

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

TEST_CASE("priority sampler unit test") {
  PrioritySampler sampler;
  auto buffer = std::make_shared<MockBuffer>();

  SECTION("doesn't discard") {
    for (const std::string &p : {"", R"(, "sampling_priority": -1)", R"(, "sampling_priority": 0)",
                                 R"(, "sampling_priority": 1)", R"(, "sampling_priority": 2)"}) {
      std::istringstream ctx(R"({"trace_id": "100", "parent_id": "100")" + p + "}");
      auto context = SpanContext::deserialize(ctx);

      REQUIRE(!sampler.discard(std::move(*static_cast<SpanContext *>(context.value().get()))));
    }
  }

  SECTION("default unconfigured priority sampling behaviour is to always sample") {
    REQUIRE(*sampler.sample("", "", 0) == SamplingPriority::SamplerKeep);
    REQUIRE(*sampler.sample("env", "service", 1) == SamplingPriority::SamplerKeep);
  }

  SECTION("configured") {
    sampler.configure("{ \"service:nginx,env:\": 0.8, \"service:nginx,env:prod\": 0.2 }"_json);

    SECTION("spans that don't match a rule are given a sampling priority of SamplerKeep") {
      REQUIRE(*sampler.sample("different env", "different service", 1) ==
              SamplingPriority::SamplerKeep);
    }

    SECTION("spans can be sampled") {
      // Case 1, service:nginx,env: => 0.8
      int count_sampled = 0;
      int total = 10000;
      for (int i = 0; i < total; i++) {
        auto p = sampler.sample("", "nginx", getId());
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
        auto p = sampler.sample("", "nginx", getId());
        REQUIRE(p != nullptr);
        REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
        count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
      }
      sample_rate = count_sampled / static_cast<double>(total);
      REQUIRE((sample_rate < 0.85 && sample_rate > 0.75));
    }
  }
}

TEST_CASE("correct sampler is used") {
  TracerOptions tracer_options{"", 0, "service_name", "web"};

  SECTION("rate sampler") {
    tracer_options.sample_rate = 0.4;
    tracer_options.priority_sampling = false;
    auto sampler = sampleProviderFromOptions(tracer_options);
    REQUIRE(std::dynamic_pointer_cast<DiscardRateSampler>(sampler));
  }

  SECTION("priority sampler") {
    tracer_options.priority_sampling = true;
    auto sampler = sampleProviderFromOptions(tracer_options);
    REQUIRE(std::dynamic_pointer_cast<PrioritySampler>(sampler));
  }
}

TEST_CASE("priority sampler \"integration\" test") {
  // There's a real integration test! It's in ./integration/nginx
  // This tests the interaction between Span and the sampler. It's a bit of an overlap with the
  // tests in span_test.cpp.
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  tracer_options.environment = "threatened by climate change";
  tracer_options.priority_sampling = true;
  auto sampler = sampleProviderFromOptions(tracer_options);
  REQUIRE(std::dynamic_pointer_cast<PrioritySampler>(sampler));

  std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
  MockHandle *handle = handle_ptr.get();
  auto only_send_traces_when_we_flush = std::chrono::seconds(3600);
  auto writer = std::make_shared<AgentWriter>(
      std::move(handle_ptr), only_send_traces_when_we_flush, 11000 /* max queued traces */,
      std::vector<std::chrono::milliseconds>{}, "hostname", 6319, sampler);
  auto buffer = std::make_shared<WritingSpanBuffer>(writer, WritingSpanBufferOptions{});

  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, getRealTime, getId, sampler}};
  const ot::StartSpanOptions span_options;
  const ot::FinishSpanOptions finish_options;

  // First, configure the PrioritySampler. Send a trace to the agent, and have the agent reply with
  // the config.
  handle->response = R"( {
    "rate_by_service": {
      "service:service_name,env:threatened by climate change": 0.3,
      "service:wrong,env:threatened by climate change": 0.1,
      "service:service_name,env:wrong": 0.5
    }
  } )";

  auto span = tracer->StartSpanWithOptions("operation_name", span_options);
  span->FinishWithOptions(finish_options);
  writer->flush(std::chrono::seconds(10));
  handle->response = "";

  SECTION("sampling rate is applied") {
    // Start a heap of spans.
    int total = 10000;
    int count_sampled = 0;
    for (int i = 0; i < total; i++) {
      auto span = tracer->StartSpanWithOptions("operation_name", span_options);
      span->FinishWithOptions(finish_options);
    }
    writer->flush(std::chrono::seconds(10));
    // Check the spans, and the rate at which they were sampled.
    auto traces = handle->getTraces();
    REQUIRE(traces->size() == total);
    for (const auto &trace : *traces) {
      REQUIRE(trace.size() == 1);
      std::cout << json(trace[0].metrics).dump() << std::endl;
      REQUIRE(trace[0].metrics.find("_sampling_priority_v1") != trace[0].metrics.end());
      OptionalSamplingPriority p =
          asSamplingPriority(trace[0].metrics.find("_sampling_priority_v1")->second);
      REQUIRE(p != nullptr);
      REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
      count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
    }
    double sample_rate = count_sampled / static_cast<double>(total);
    REQUIRE((sample_rate < 0.35 && sample_rate > 0.25));
  }
}
