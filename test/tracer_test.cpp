#include "../src/tracer.h"
#include <unistd.h>
#include <ctime>
#include "../src/sample.h"
#include "../src/span.h"
#include "../src/tracer_options.h"
#include "mocks.h"

#include <datadog/tags.h>
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
  auto logger = std::make_shared<const MockLogger>();
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, get_time, get_id}};
  const ot::StartSpanOptions span_options;

  SECTION("names spans correctly") {
    auto span = tracer->StartSpanWithOptions("/what_up", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
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

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 100);
    REQUIRE(result->parent_id == 0);
  }

  SECTION("span context is propagated") {
    MockTextMapCarrier carrier;
    SpanContext context{logger, 420, 69, "", {{"ayy", "lmao"}, {"hi", "haha"}}};
    auto success = tracer->Inject(context, carrier);
    REQUIRE(success);
    auto span_context_maybe = tracer->Extract(carrier);
    REQUIRE(span_context_maybe);
    auto span = tracer->StartSpan("fred", {ChildOf(span_context_maybe->get())});
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);
    auto& result = buffer->traces().at(69).finished_spans->at(0);
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
    auto& result = buffer->traces().at(100).finished_spans->at(0);
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
    auto& root_result = buffer->traces().at(100).finished_spans->at(1);
    REQUIRE(root_result->meta["_dd.hostname"] == "testhostname");

    // Tag should not exist on the child span(s).
    auto& child_result = buffer->traces().at(100).finished_spans->at(0);
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
    auto& root_result = buffer->traces().at(100).finished_spans->at(1);
    REQUIRE(root_result->metrics["_dd1.sr.eausr"] == 1.0);

    // Tag should not exist on the child span(s).
    auto& child_result = buffer->traces().at(100).finished_spans->at(0);
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
  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::make_shared<MockWriter>(sampler);
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  const ot::StartSpanOptions span_options;

  struct EnvOverrideTest {
    std::string env;
    std::string val;
    bool enabled;
    std::string hostname;
    double rate;
    bool error;
    std::string environment;
    std::string version;
    std::map<std::string, std::string> extra_tags;
  };

  char buf[256];
  ::gethostname(buf, 256);
  std::string hostname(buf);

  auto env_test = GENERATE_COPY(values<EnvOverrideTest>({
      // Normal cases
      {"DD_TRACE_ENABLED", "false", false, "", std::nan(""), false, "test-env", "", {}},
      {"DD_ENV", "test-env", true, "", std::nan(""), false, "test-env", "", {}},
      {"DD_VERSION",
       "test-version v0.0.1",
       true,
       "",
       std::nan(""),
       false,
       "",
       "test-version v0.0.1",
       {}},
      {"DD_TRACE_REPORT_HOSTNAME", "true", true, hostname, std::nan(""), false, "", "", {}},
      {"DD_TRACE_ANALYTICS_ENABLED", "true", true, "", 1.0, false, "", "", {}},
      {"DD_TRACE_ANALYTICS_ENABLED", "false", true, "", 0.0, false, "", "", {}},
      {"DD_TRACE_ANALYTICS_SAMPLE_RATE", "0.5", true, "", 0.5, false, "", "", {}},
      {"DD_TAGS",
       "host:my-host-name,region:us-east-1,datacenter:us,partition:5",
       true,
       "",
       std::nan(""),
       false,
       "",
       "",
       {
           {"host", "my-host-name"},
           {"region", "us-east-1"},
           {"datacenter", "us"},
           {"partition", "5"},
       }},
      {"", "", true, "", std::nan(""), false, "", "", {}},
      // Unexpected values handled gracefully
      {"DD_TRACE_ANALYTICS_ENABLED", "yes please", true, "", std::nan(""), true, "", "", {}},
      {"DD_TRACE_ANALYTICS_SAMPLE_RATE", "1.1", true, "", std::nan(""), true, "", "", {}},
      {"DD_TRACE_ANALYTICS_SAMPLE_RATE", "half", true, "", std::nan(""), true, "", "", {}},
  }));

  SECTION("set correct tags and metrics") {
    // Setup
    ::setenv(env_test.env.c_str(), env_test.val.c_str(), 0);
    auto maybe_options = applyTracerOptionsFromEnvironment(tracer_options);
    if (env_test.error) {
      REQUIRE(maybe_options.error());
      return;
    }
    REQUIRE(maybe_options);
    TracerOptions opts = maybe_options.value();
    opts.log_func = [](LogLevel, ot::string_view) {};  // noise suppression
    std::shared_ptr<Tracer> tracer{new Tracer{opts, writer, sampler}};

    // Create span
    auto span = tracer->StartSpanWithOptions("/env-override", span_options);
    const ot::FinishSpanOptions finish_options;
    span->FinishWithOptions(finish_options);

    if (env_test.enabled) {
      auto& result = writer->traces[0][0];
      // Check the hostname matches the expected value.
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
      // Check the environment matches the expected value.
      if (env_test.environment.empty()) {
        REQUIRE(result->meta.find(datadog::tags::environment) == result->meta.end());
      } else {
        REQUIRE(result->meta[datadog::tags::environment] == env_test.environment);
      }
      // Check the version matches the expected value.
      if (env_test.version.empty()) {
        REQUIRE(result->meta.find(datadog::tags::version) == result->meta.end());
      } else {
        REQUIRE(result->meta[datadog::tags::version] == env_test.version);
      }
      // Check spans are tagged with values from DD_TAGS
      for (auto& tag : env_test.extra_tags) {
        REQUIRE(result->meta.find(tag.first) != result->meta.end());
        REQUIRE(result->meta[tag.first] == tag.second);
      }
    } else {
      REQUIRE(writer->traces.empty());
    }

    // Tear-down
    ::unsetenv(env_test.env.c_str());
  }
}

TEST_CASE("startup log") {
  auto enabled = GENERATE(false, true);
  std::string env_var = "DD_TRACE_STARTUP_LOGS";
  if (!enabled) {
    ::setenv(env_var.c_str(), "false", 0);
  }
  TracerOptions opts;
  opts.agent_url = "/path/to/unix.socket";
  opts.analytics_rate = 0.3;
  opts.tags.emplace("foo", "bar");
  opts.tags.emplace("themeaningoflifetheuniverseandeverything", "42");
  opts.operation_name_override = "meaningful.name";
  std::stringstream ss;
  opts.log_func = [&](LogLevel, ot::string_view message) { ss << message; };

  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::make_shared<MockWriter>(sampler);
  std::shared_ptr<Tracer> tracer{new Tracer{opts, writer, sampler}};

  if (enabled) {
    REQUIRE(!ss.str().empty());
    std::string log_message = ss.str();
    std::string prefix = "DATADOG TRACER CONFIGURATION - ";
    REQUIRE(log_message.substr(0, prefix.size()) == prefix);
    json j = json::parse(log_message.substr(prefix.size()));  // may throw an exception
    REQUIRE(j["date"].get<std::string>().size() == 24);
    REQUIRE(j["agent_url"] == opts.agent_url);
    REQUIRE(j["analytics_sample_rate"] == opts.analytics_rate);
    REQUIRE(j["tags"] == opts.tags);
    REQUIRE(j["operation_name_override"] == opts.operation_name_override);
  } else {
    REQUIRE(ss.str().empty());
  }
  ::unsetenv(env_var.c_str());
}
