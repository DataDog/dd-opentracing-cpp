#include "../src/tracer.h"
#include <unistd.h>
#include <ctime>
#include "../src/sample.h"
#include "../src/span.h"
#include "mocks.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer") {
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
  auto sampler = std::make_shared<KeepAllSampler>();
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, get_time, get_id, sampler}};
  const ot::StartSpanOptions span_options;

  SECTION("names spans correctly") {
    auto span = tracer->StartSpanWithOptions("/what_up", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->type == "web");
    REQUIRE(result->service == "service_name");
    REQUIRE(result->name == "/what_up");
    REQUIRE(result->resource == "/what_up");
    REQUIRE(result->meta.find("_dd.hostname") == result->meta.end());
    REQUIRE(result->metrics.find("_dd1.sr.eausr") == result->metrics.end());
  }

  SECTION("spans receive id") {
    auto span = tracer->StartSpanWithOptions("", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 100);
    REQUIRE(result->parent_id == 0);
  }

  SECTION("span context is propagated") {
    MockTextMapCarrier carrier;
    SpanContext context{420, 69, "", {{"ayy", "lmao"}, {"hi", "haha"}}};
    auto success = tracer->Inject(context, carrier);
    REQUIRE(success);
    auto span_context_maybe = tracer->Extract(carrier);
    REQUIRE(span_context_maybe);
    auto span = tracer->StartSpan("fred", {ChildOf(span_context_maybe->get())});
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);
    auto& result = buffer->traces(69).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 69);
    REQUIRE(result->parent_id == 420);
  }

  SECTION("empty span context starts new root span") {
    MockTextMapCarrier carrier;
    auto span_context_maybe = tracer->Extract(carrier);
    auto span = tracer->StartSpan("fred", {ChildOf(span_context_maybe->get())});
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);
    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 100);
    REQUIRE(result->parent_id == 0);
  }

  SECTION("hostname is added as a tag") {
    buffer->setHostname("testhostname");
    auto root_span = tracer->StartSpanWithOptions("/root", span_options);
    auto child_span = tracer->StartSpan("/child", {ChildOf(&root_span->context())});
    root_span->SetTag("foo", "bar");
    child_span->SetTag("baz", "bing");
    const ot::FinishSpanOptions finish_options;
    child_span->FinishWithOptions(finish_options);
    root_span->FinishWithOptions(finish_options);
    buffer->setHostname("");

    // Tag should exist with the correct value on the root / local-root span.
    auto& root_result = buffer->traces(100).finished_spans->at(1);
    REQUIRE(root_result->meta["_dd.hostname"] == "testhostname");

    // Tag should not exist on the child span(s).
    auto& child_result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(child_result->meta.find("_dd.hostname") == child_result->meta.end());
  }

  SECTION("analytics rate is added as a metric") {
    buffer->setAnalyticsRate(1.0);
    auto root_span = tracer->StartSpanWithOptions("/root", span_options);
    auto child_span = tracer->StartSpan("/child", {ChildOf(&root_span->context())});
    root_span->SetTag("foo", "bar");
    child_span->SetTag("baz", "bing");
    const ot::FinishSpanOptions finish_options;
    child_span->FinishWithOptions(finish_options);
    root_span->FinishWithOptions(finish_options);
    buffer->setAnalyticsRate(std::nan(""));

    // Metric should exist with the correct value on the root / local-root span.
    auto& root_result = buffer->traces(100).finished_spans->at(1);
    REQUIRE(root_result->metrics["_dd1.sr.eausr"] == 1.0);

    // Tag should not exist on the child span(s).
    auto& child_result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(child_result->metrics.find("_dd1.sr.eausr") == child_result->metrics.end());
  }
}

TEST_CASE("env overrides") {
  int id = 100;  // Starting span id.
  // Starting calendar time 2007-03-12 00:00:00
  std::tm start{};
  start.tm_mday = 12;
  start.tm_mon = 2;
  start.tm_year = 107;
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  // auto buffer = std::make_shared<MockBuffer>();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  auto sampler = std::make_shared<KeepAllSampler>();
  auto mwriter = std::make_shared<MockWriter>(sampler);
  auto writer = std::shared_ptr<Writer>(mwriter);
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  const ot::StartSpanOptions span_options;

  struct EnvOverrideTest {
    std::string env;
    std::string val;
    std::string hostname;
    double rate;
  };

  char buf[256];
  ::gethostname(buf, 256);
  std::string hostname(buf);

  auto env_test = GENERATE_COPY(
      values<EnvOverrideTest>({// Normal cases
                               {"DD_TRACE_REPORT_HOSTNAME", "true", hostname, std::nan("")},
                               {"DD_TRACE_ANALYTICS_ENABLED", "true", "", 1.0},
                               {"DD_TRACE_ANALYTICS_ENABLED", "false", "", 0.0},
                               {"DD_GLOBAL_ANALYTICS_SAMPLE_RATE", "0.5", "", 0.5},
                               {"", "", "", std::nan("")},
                               // Unexpected values handled gracefully
                               {"DD_TRACE_ANALYTICS_ENABLED", "yes please", "", std::nan("")},
                               {"DD_GLOBAL_ANALYTICS_SAMPLE_RATE", "1.1", "", std::nan("")},
                               {"DD_GLOBAL_ANALYTICS_SAMPLE_RATE", "half", "", std::nan("")}}));

  SECTION("set correct tags and metrics") {
    // Setup
    ::setenv(env_test.env.c_str(), env_test.val.c_str(), 0);
    std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, writer, sampler}};

    // Create span
    auto span = tracer->StartSpanWithOptions("/env-override", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto& result = mwriter->traces[0][0];
    // Check the analytics rate matches the expected value.
    if (env_test.hostname.empty()) {
      REQUIRE(result->meta.find("_dd.hostname") == result->meta.end());
    } else {
      REQUIRE(result->meta["_dd.hostname"] == env_test.hostname);
    }
    // Check the analytics rate matches the expected value.
    if (std::isnan(env_test.rate)) {
      REQUIRE(result->metrics.find("_dd1.sr.eausr") == result->metrics.end());
    } else {
      REQUIRE(result->metrics["_dd1.sr.eausr"] == env_test.rate);
    }
    // Tear-down
    ::unsetenv(env_test.env.c_str());
  }
}
