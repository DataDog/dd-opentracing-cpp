#include "../src/span.h"
#include <datadog/tags.h>
#include <opentracing/ext/tags.h>
#include <ctime>
#include <nlohmann/json.hpp>
#include <thread>
#include "../src/sample.h"
#include "mocks.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;
namespace tags = datadog::tags;
using json = nlohmann::json;

TEST_CASE("span") {
  int id = 100;  // Starting span id.
  // Starting calendar time 2007-03-12 00:00:00
  std::tm start{};
  start.tm_mday = 12;
  start.tm_mon = 2;
  start.tm_year = 107;
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto sampler = std::make_shared<KeepAllSampler>();
  auto buffer = std::make_shared<MockBuffer>();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  const ot::FinishSpanOptions finish_options;

  SECTION("receives id") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 100);
    REQUIRE(result->parent_id == 0);
  }

  SECTION("registers with SpanBuffer") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};
    REQUIRE(buffer->traces().size() == 1);
    REQUIRE(buffer->traces().find(100) != buffer->traces().end());
    REQUIRE(buffer->traces(100).finished_spans->size() == 0);
    REQUIRE(buffer->traces(100).all_spans.size() == 1);
  }

  SECTION("timed correctly") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};
    advanceSeconds(time, 10);
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->duration == 10000000000);
  }

  SECTION("audits span data") {
    std::list<std::pair<std::string, std::string>> test_cases{
        // Should remove query params
        {"/", "/"},
        {"/?asdf", "/?"},
        {"/search", "/search"},
        {"/search?", "/search?"},
        {"/search?id=100&private=true", "/search?"},
        {"/search?id=100&private=true?", "/search?"},
        {"http://i-012a3b45c6d78901e//api/v1/check_run?api_key=0abcdef1a23b4c5d67ef8a90b1cde234",
         "http://?//api/v1/check_run?"},
        // Should replace all digits
        {"/1", "/?"},
        {"/9999", "/?"},
        {"/user/1", "/user/?"},
        {"/user/1/", "/user/?/"},
        {"/user/1/repo/50", "/user/?/repo/?"},
        {"/user/1/repo/50/", "/user/?/repo/?/"},
        // Should replace segments with mixed-characters
        {"/a1/v2", "/?/?"},
        {"/v3/1a", "/v3/?"},
        {"/V01/v9/abc/-1?", "/V01/v9/abc/?"},
        {"/ABC/av-1/b_2/c.3/d4d/v5f/v699/7", "/ABC/?/?/?/?/?/?/?"},
        {"/user/asdf123/repository/01234567-9ABC-DEF0-1234", "/user/?/repository/?"},
        {"/ABC/a-1/b_2/c.3/d4d/5f/6", "/ABC/?/?/?/?/?/?"}};

    std::shared_ptr<SpanBuffer> buffer_ptr{buffer};
    for (auto& test_case : test_cases) {
      auto span_id = get_id();
      Span span{nullptr,    buffer_ptr, get_time, sampler,
                span_id,    span_id,    0,        SpanContext{span_id, span_id, "", {}},
                get_time(), "",         "",       "",
                "",         ""};
      span.SetTag(ot::ext::http_url, test_case.first);
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(span_id).finished_spans->back();
      REQUIRE(result->meta.find(ot::ext::http_url)->second == test_case.second);
    }
  }

  SECTION("finishes once") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&]() { span.FinishWithOptions(finish_options); });
    }
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    REQUIRE(buffer->traces().size() == 1);
    REQUIRE(buffer->traces().find(100) != buffer->traces().end());
    REQUIRE(buffer->traces(100).finished_spans->size() == 1);
  }

  SECTION("handles tags") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};

    span.SetTag("bool", true);
    span.SetTag("double", 6.283185);
    span.SetTag("int64_t", -69);
    span.SetTag("uint64_t", 420);
    span.SetTag("string", std::string("hi there"));
    span.SetTag("nullptr", nullptr);
    span.SetTag("char*", "hi there");
    span.SetTag("list", std::vector<ot::Value>{"hi", 420, true});
    span.SetTag("map", std::unordered_map<std::string, ot::Value>{
                           {"a", "1"},
                           {"b", 2},
                           {"c", std::unordered_map<std::string, ot::Value>{{"nesting", true}}}});

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    // Check "map" seperately, because JSON key order is non-deterministic therefore we can't do
    // simple string matching.
    REQUIRE(json::parse(result->meta["map"]) ==
            json::parse(R"({"a":"1","b":2,"c":{"nesting":true}})"));
    result->meta.erase("map");
    // Check the rest.
    REQUIRE(result->meta == std::unordered_map<std::string, std::string>{
                                {"bool", "true"},
                                {"double", "6.283185"},
                                {"int64_t", "-69"},
                                {"uint64_t", "420"},
                                {"string", "hi there"},
                                {"nullptr", "nullptr"},
                                {"char*", "hi there"},
                                {"list", "[\"hi\",420,true]"},
                            });
  }

  SECTION("replaces colons with dots in tag key") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};

    span.SetTag("foo:bar:baz", "x");

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->meta == std::unordered_map<std::string, std::string>{
                                {"foo.bar.baz", "x"},
                            });
  }

  SECTION("maps datadog tags to span data") {
    auto span_id = get_id();
    Span span{nullptr,
              buffer,
              get_time,
              sampler,
              span_id,
              span_id,
              0,
              SpanContext{span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              ""};
    span.SetTag(tags::service_name, "new service");
    span.SetTag(tags::span_type, "new type");
    span.SetTag(tags::resource_name, "new resource");
    span.SetTag(tags::analytics_event, true);
    span.SetTag("tag with no special meaning", "ayy lmao");

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    // Datadog special tags aren't kept, they just set the Span values.
    REQUIRE(result->meta == std::unordered_map<std::string, std::string>{
                                {"tag with no special meaning", "ayy lmao"}});
    REQUIRE(result->name == "original span name");
    REQUIRE(result->service == "new service");
    REQUIRE(result->type == "new type");
    REQUIRE(result->resource == "new resource");
    REQUIRE(result->metrics == std::unordered_map<std::string, double>{{"_dd1.sr.eausr", 1}});
  }

  SECTION("values for analytics_event tag") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};

    struct AnalyticsEventTagTestCase {
      ot::Value tag_value;
      bool expected;
      double metric_value;
    };
    auto test_case =
        GENERATE(values<AnalyticsEventTagTestCase>({{true, true, 1.0},
                                                    {false, true, 0.0},
                                                    {1, true, 1.0},
                                                    {0, true, 0.0},
                                                    {1.0, true, 1.0},
                                                    {0.5, true, 0.5},
                                                    {0.0, true, 0.0},
                                                    {"", true, 0.0},
                                                    {-1, false, 0},
                                                    {2, false, 0},
                                                    {-0.1, false, 0},
                                                    {1.1, false, 0},
                                                    {"not a number at all", false, 0}}));

    span.SetTag(tags::analytics_event, test_case.tag_value);
    span.FinishWithOptions(finish_options);
    auto& result = buffer->traces(100).finished_spans->at(0);
    auto metric = result->metrics.find("_dd1.sr.eausr");

    if (test_case.expected) {
      REQUIRE(metric != result->metrics.end());
      REQUIRE(metric->second == test_case.metric_value);
    } else {
      REQUIRE(metric == result->metrics.end());
    }
  }

  SECTION("error tag sets error") {
    auto span_id = get_id();
    Span span{nullptr,    buffer,  get_time, sampler,
              span_id,    span_id, 0,        SpanContext{span_id, span_id, "", {}},
              get_time(), "",      "",       "",
              "",         ""};

    struct ErrorTagTestCase {
      ot::Value value;
      uint32_t span_error;
      std::string span_tag;
    };

    auto error_tag_test_case = GENERATE(values<ErrorTagTestCase>({
        {"0", 0, "0"},
        {0, 0, "0"},
        {"", 0, ""},
        {"false", 0, "false"},
        {false, 0, "false"},
        {"1", 1, "1"},
        {1, 1, "1"},
        {"any random truth-ish string or value lol", 1,
         "any random truth-ish string or value lol"},
        {std::vector<ot::Value>{"hi", 420, true}, 1, "[\"hi\",420,true]"},
        {"true", 1, "true"},
        {true, 1, "true"},
    }));

    span.SetTag("error", error_tag_test_case.value);
    span.FinishWithOptions(finish_options);
    auto& result = buffer->traces(100).finished_spans->at(0);

    REQUIRE(result->error == error_tag_test_case.span_error);
    REQUIRE(result->meta["error"] == error_tag_test_case.span_tag);
  }

  SECTION("operation name can be overridden") {
    auto span_id = get_id();
    Span span{nullptr,
              buffer,
              get_time,
              sampler,
              span_id,
              span_id,
              0,
              SpanContext{span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              "overridden operation name"};

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->meta ==
            std::unordered_map<std::string, std::string>{{"operation", "original span name"}});
    REQUIRE(result->name == "overridden operation name");
    REQUIRE(result->resource == "original resource");
    REQUIRE(result->service == "original service");
    REQUIRE(result->type == "original type");
  }

  SECTION("special resource tag has priority over operation name override") {
    auto span_id = get_id();
    Span span{nullptr,
              buffer,
              get_time,
              sampler,
              span_id,
              span_id,
              0,
              SpanContext{span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              "overridden operation name"};

    span.SetTag("resource.name", "new resource");
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces(100).finished_spans->at(0);
    REQUIRE(result->meta ==
            std::unordered_map<std::string, std::string>{{"operation", "original span name"}});
    REQUIRE(result->name == "overridden operation name");
    REQUIRE(result->resource == "new resource");
    REQUIRE(result->service == "original service");
    REQUIRE(result->type == "original type");
  }

  SECTION("OpenTracing operation name works") {
    auto span_id = get_id();
    Span span{nullptr,
              buffer,
              get_time,
              sampler,
              span_id,
              span_id,
              0,
              SpanContext{span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              ""};
    span.SetOperationName("operation name");

    SECTION("sets resource and span name") {
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(100).finished_spans->at(0);
      REQUIRE(result->name == "operation name");
      REQUIRE(result->resource == "operation name");
    }

    SECTION("sets resource, but can be overridden by Datadog tag") {
      span.SetTag("resource.name", "resource tag override");
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(100).finished_spans->at(0);
      REQUIRE(result->name == "operation name");
      REQUIRE(result->resource == "resource tag override");
    }
  }

  SECTION("sampling") {
    auto priority_sampler = std::make_shared<MockSampler>();
    priority_sampler->sampling_priority =
        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);

    SECTION("root spans may be sampled") {
      Span span{nullptr,    buffer, get_time, priority_sampler,
                100,        100,    0,        SpanContext{100, 100, "", {}},
                get_time(), "",     "",       "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(100).finished_spans->at(0);
      REQUIRE(result->metrics ==
              std::unordered_map<std::string, double>{{"_sampling_priority_v1", 1}});
    }

    SECTION("non-root spans may be sampled, as long as the trace is not yet distributed") {
      Span span{nullptr,    buffer,
                get_time,   priority_sampler,
                100,        42,
                42,         SpanContext{100, 42, "", {}},  // Non-distributed SpanContext
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      REQUIRE(*buffer->traces(42).sampling_priority == SamplingPriority::SamplerKeep);
    }

    SECTION(
        "with priority sampling enabled, propagated spans without a sampling priority will be "
        "sampled even though they're not the root") {
      // parent_id is decoded to span_id, and the tracer will create a child context with the
      // span_id set to the span it's for. In this case we're deserializing (so as to simulate
      // propagation) but directly passing to the Span; so we encode parent_id as the id of the
      // span we're passing to.
      std::istringstream ctx(R"({
            "trace_id": "42",
            "parent_id": "100"
          })");
      auto context = SpanContext::deserialize(ctx);
      Span span{nullptr,    buffer,
                get_time,   priority_sampler,
                100,        42,
                42,         std::move(*static_cast<SpanContext*>(context.value().get())),
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(42).finished_spans->at(0);
      REQUIRE(result->metrics ==
              std::unordered_map<std::string, double>{{"_sampling_priority_v1", 1}});
    }

    SECTION("spans with an existing sampling priority may not be given a new one at Finish") {
      std::istringstream ctx(R"({
            "trace_id": "100",
            "parent_id": "100",
            "sampling_priority": -1
          })");
      auto context = SpanContext::deserialize(ctx);
      Span span{nullptr,    buffer,
                get_time,   priority_sampler,
                100,        100,
                0,          std::move(*static_cast<SpanContext*>(context.value().get())),
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces(100).finished_spans->at(0);
      REQUIRE(result->metrics ==
              std::unordered_map<std::string, double>{{"_sampling_priority_v1", -1}});
    }
  }
}
