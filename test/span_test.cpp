#include "../src/span.h"

#include <datadog/tags.h>
#include <opentracing/ext/tags.h>

#include <catch2/catch.hpp>
#include <ctime>
#include <nlohmann/json.hpp>
#include <ostream>
#include <thread>
#include <vector>

#include "../src/sample.h"
#include "../src/tag_propagation.h"
#include "mocks.h"

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
  auto buffer = std::make_shared<MockBuffer>();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  const ot::FinishSpanOptions finish_options;
  auto logger = std::make_shared<const MockLogger>();

  SECTION("receives id") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->span_id == 100);
    REQUIRE(result->trace_id == 100);
    REQUIRE(result->parent_id == 0);
  }

  SECTION("registers with SpanBuffer") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};
    REQUIRE(buffer->traces().size() == 1);
    REQUIRE(buffer->traces().find(100) != buffer->traces().end());
    REQUIRE(buffer->traces().at(100).finished_spans->size() == 0);
    REQUIRE(buffer->traces().at(100).all_spans.size() == 1);
  }

  SECTION("timed correctly") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};
    advanceTime(time, std::chrono::seconds(10));
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->duration == 10000000000);
  }

  SECTION("audits span data (just URL parameters)") {
    std::list<std::pair<std::string, std::string>> test_cases{
        // Should remove query params
        {"/", "/"},
        {"/?asdf", "/"},
        {"/search", "/search"},
        {"/search?", "/search"},
        {"/search?id=100&private=true", "/search"},
        {"/search?id=100&private=true?", "/search"},
        {"http://i-012a3b45c6d78901e//api/v1/check_run?api_key=0abcdef1a23b4c5d67ef8a90b1cde234",
         "http://i-012a3b45c6d78901e//api/v1/check_run"},
    };

    std::shared_ptr<SpanBuffer> buffer_ptr{buffer};
    for (auto& test_case : test_cases) {
      auto span_id = get_id();
      Span span{logger,     nullptr, buffer_ptr, get_time,
                span_id,    span_id, 0,          SpanContext{logger, span_id, span_id, "", {}},
                get_time(), "",      "",         "",
                "",         ""};
      span.SetTag(ot::ext::http_url, test_case.first);
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(span_id).finished_spans->back();
      REQUIRE(result->meta.find(ot::ext::http_url)->second == test_case.second);
    }
  }

  SECTION("audits span data (legacy)") {
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
      Span span{logger,     nullptr, buffer_ptr, get_time,
                span_id,    span_id, 0,          SpanContext{logger, span_id, span_id, "", {}},
                get_time(), "",      "",         "",
                "",         "",      true};
      span.SetTag(ot::ext::http_url, test_case.first);
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(span_id).finished_spans->back();
      REQUIRE(result->meta.find(ot::ext::http_url)->second == test_case.second);
    }
  }

  SECTION("finishes once") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&]() { span.FinishWithOptions(finish_options); });
    }
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    REQUIRE(buffer->traces().size() == 1);
    REQUIRE(buffer->traces().find(100) != buffer->traces().end());
    REQUIRE(buffer->traces().at(100).finished_spans->size() == 1);
  }

  SECTION("handles tags") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
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

    auto& result = buffer->traces().at(100).finished_spans->at(0);
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
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};

    span.SetTag("foo:bar:baz", "x");

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->meta == std::unordered_map<std::string, std::string>{
                                {"foo.bar.baz", "x"},
                            });
  }

  SECTION("maps datadog tags to span data") {
    auto span_id = get_id();
    Span span{logger,
              nullptr,
              buffer,
              get_time,
              span_id,
              span_id,
              0,
              SpanContext{logger, span_id, span_id, "", {}},
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

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    // Datadog special tags aren't kept, they just set the Span values.
    REQUIRE(result->meta.find("tag with no special meaning") != result->meta.end());
    REQUIRE(result->meta.find("tag with no special meaning")->second == "ayy lmao");
    REQUIRE(result->name == "original span name");
    REQUIRE(result->service == "new service");
    REQUIRE(result->type == "new type");
    REQUIRE(result->resource == "new resource");
    REQUIRE(result->metrics.find("_dd1.sr.eausr") != result->metrics.end());
    REQUIRE(result->metrics["_dd1.sr.eausr"] == 1.0);
  }

  SECTION("values for analytics_event tag") {
    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
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
    auto& result = buffer->traces().at(100).finished_spans->at(0);
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
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};

    struct ErrorTagTestCase {
      ot::Value value;
      uint32_t span_error;
      std::string span_tag;
    };

    auto error_tag_test_case = GENERATE(values<ErrorTagTestCase>({
        {"0", 0, ""},
        {0, 0, ""},
        {"", 0, ""},
        {"false", 0, ""},
        {false, 0, ""},
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
    auto& result = buffer->traces().at(100).finished_spans->at(0);

    REQUIRE(result->error == error_tag_test_case.span_error);
    REQUIRE(result->meta["error"] == error_tag_test_case.span_tag);
  }

  SECTION("error.* tags override error tag") {
    // The tag name `opentracing::ext::error` is "error", which is also the
    // first part of the nested tags "error.msg", "error.stack", and
    // "error.type".  The latter tags are significant to Error Tracking
    // <https://docs.datadoghq.com/tracing/error_tracking/>.
    //
    // It was found that setting the "error" tag makes some parts of Datadog
    // behave as if all "error.*" tags had been removed.  A user who set both
    // the "error" tag and the "error.msg" tag, for example, might find that
    // only the "error" tag appeared in the Datadog UI.
    //
    // This test section verifies that the above does not happen.

    auto span_id = get_id();
    Span span{logger,     nullptr, buffer, get_time,
              span_id,    span_id, 0,      SpanContext{logger, span_id, span_id, "", {}},
              get_time(), "",      "",     "",
              "",         ""};

    using OptionalString = ot::util::variant<std::nullptr_t, std::string>;

    // For each member of `ErrorTags`, `nullptr` denotes the absence of the tag.
    struct ErrorTags {
      OptionalString error;  // "error" tag
      OptionalString msg;    // "error.msg" tag
      OptionalString stack;  // "error.stack" tag
      OptionalString type;   // "error.type" tag
    };

    struct Case {
      int index;        // for debugging
      ErrorTags before; // before span finishes
      ErrorTags after;  // after span finishes
      bool error_property_after;
    };

    // clang-format off
    auto test_case = GENERATE(values<Case>({
          //   Before span finishes                After span finishes                   error?
          //   ----------------------------------  ------------------------------------
          //   error    .msg    .stack   .type      error    .msg     .stack   .type
          //   ----------------------------------  --------------------------------------------
          // No error tags means no error.
          {0,  {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}, false},
          // Setting any of the "error.*" tags sets the error property.
          {1,  {nullptr, "dummy", nullptr, nullptr}, {nullptr, "dummy", nullptr, nullptr}, true},
          {2,  {nullptr, nullptr, "dummy", nullptr}, {nullptr, nullptr, "dummy", nullptr}, true},
          {3,  {nullptr, nullptr, nullptr, "dummy"}, {nullptr, nullptr, nullptr, "dummy"}, true},
          // "error" without "error.*" leaves just "error".
          {4,  {"true",  nullptr, nullptr, nullptr}, {"true",  nullptr, nullptr, nullptr}, true},
          // Setting "error.*" unsets "error", but keeps the error property.
          {5,  {"true",  "dummy", nullptr, nullptr}, {nullptr, "dummy", nullptr, nullptr}, true},
          {6,  {"true",  nullptr, "dummy", nullptr}, {nullptr, nullptr, "dummy", nullptr}, true},
          {7,  {"true",  nullptr, nullptr, "dummy"}, {nullptr, nullptr, nullptr, "dummy"}, true},
          // If "error" is falsy, then "error" and "error.*" tags are removed, and no error.
          {8,  {"false", "dummy", "dummy", "dummy"}, {nullptr, nullptr, nullptr, nullptr}, false},
          {9,  {"0",     "dummy", "dummy", "dummy"}, {nullptr, nullptr, nullptr, nullptr}, false},
          {10, {"",      "dummy", "dummy", "dummy"}, {nullptr, nullptr, nullptr, nullptr}, false},
    }));
    // clang-format on

    CAPTURE(test_case.index);

    if (test_case.before.error != nullptr) {
      span.SetTag("error", test_case.before.error.get<std::string>());
    }
    if (test_case.before.msg!= nullptr) {
      span.SetTag("error.msg", test_case.before.msg.get<std::string>());
    }
    if (test_case.before.stack != nullptr) {
      span.SetTag("error.stack", test_case.before.stack.get<std::string>());
    }
    if (test_case.before.type != nullptr) {
      span.SetTag("error.type", test_case.before.type.get<std::string>());
    }

    span.FinishWithOptions(finish_options);
    const auto& after = *buffer->traces().at(100).finished_spans->at(0);

    REQUIRE(after.error == int(test_case.error_property_after));

    if (test_case.after.error == nullptr) {
      REQUIRE(after.meta.count("error") == 0);
    } else {
      REQUIRE(after.meta.count("error") == 1);
      REQUIRE(after.meta.at("error") == test_case.after.error.get<std::string>());
    }
    if (test_case.after.msg == nullptr) {
      REQUIRE(after.meta.count("error.msg") == 0);
    } else {
      REQUIRE(after.meta.count("error.msg") == 1);
      REQUIRE(after.meta.at("error.msg") == test_case.after.msg.get<std::string>());
    }
    if (test_case.after.stack == nullptr) {
      REQUIRE(after.meta.count("error.stack") == 0);
    } else {
      REQUIRE(after.meta.count("error.stack") == 1);
      REQUIRE(after.meta.at("error.stack") == test_case.after.stack.get<std::string>());
    }
    if (test_case.after.type == nullptr) {
      REQUIRE(after.meta.count("error.type") == 0);
    } else {
      REQUIRE(after.meta.count("error.type") == 1);
      REQUIRE(after.meta.at("error.type") == test_case.after.type.get<std::string>());
    }
  }

  SECTION("operation name can be overridden") {
    auto span_id = get_id();
    Span span{logger,
              nullptr,
              buffer,
              get_time,
              span_id,
              span_id,
              0,
              SpanContext{logger, span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              "overridden operation name"};

    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->meta ==
            std::unordered_map<std::string, std::string>{{"operation", "original span name"}});
    REQUIRE(result->name == "overridden operation name");
    REQUIRE(result->resource == "original resource");
    REQUIRE(result->service == "original service");
    REQUIRE(result->type == "original type");
  }

  SECTION("special resource tag has priority over operation name override") {
    auto span_id = get_id();
    Span span{logger,
              nullptr,
              buffer,
              get_time,
              span_id,
              span_id,
              0,
              SpanContext{logger, span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              "overridden operation name"};

    span.SetTag("resource.name", "new resource");
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->meta ==
            std::unordered_map<std::string, std::string>{{"operation", "original span name"}});
    REQUIRE(result->name == "overridden operation name");
    REQUIRE(result->resource == "new resource");
    REQUIRE(result->service == "original service");
    REQUIRE(result->type == "original type");
  }

  SECTION("OpenTracing operation name works") {
    auto span_id = get_id();
    Span span{logger,
              nullptr,
              buffer,
              get_time,
              span_id,
              span_id,
              0,
              SpanContext{logger, span_id, span_id, "", {}},
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

      auto& result = buffer->traces().at(100).finished_spans->at(0);
      REQUIRE(result->name == "operation name");
      REQUIRE(result->resource == "operation name");
    }

    SECTION("sets resource, but can be overridden by Datadog tag") {
      span.SetTag("resource.name", "resource tag override");
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(100).finished_spans->at(0);
      REQUIRE(result->name == "operation name");
      REQUIRE(result->resource == "resource tag override");
    }
  }

  SECTION("SetOperationName updates the tag but not the overridden name") {
    auto span_id = get_id();
    Span span{logger,
              nullptr,
              buffer,
              get_time,
              span_id,
              span_id,
              0,
              SpanContext{logger, span_id, span_id, "", {}},
              get_time(),
              "original service",
              "original type",
              "original span name",
              "original resource",
              "overridden name"};
    span.SetOperationName("updated operation name");
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces().at(100).finished_spans->at(0);
    REQUIRE(result->name == "overridden name");
    REQUIRE(result->resource == "updated operation name");
    REQUIRE(result->meta[tags::operation_name] == "updated operation name");
  }

  SECTION("priority sampling") {
    SECTION("root spans may be sampled") {
      Span span{logger,     nullptr, buffer, get_time,
                100,        100,     0,      SpanContext{logger, 100, 100, "", {}},
                get_time(), "",      "",     "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(100).finished_spans->at(0);
      REQUIRE(result->metrics.find("_sampling_priority_v1") != result->metrics.end());
      REQUIRE(result->metrics["_sampling_priority_v1"] == 1);
    }

    SECTION("non-root spans may be sampled, as long as the trace is not yet distributed") {
      Span span{logger,     nullptr,
                buffer,     get_time,
                100,        42,
                42,         SpanContext{logger, 100, 42, "", {}},  // Non-distributed SpanContext
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      REQUIRE(*buffer->traces().at(42).sampling_priority == SamplingPriority::SamplerKeep);
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
      auto context = SpanContext::deserialize(logger, ctx);
      Span span{logger,     nullptr,
                buffer,     get_time,
                100,        42,
                42,         std::move(*static_cast<SpanContext*>(context.value().get())),
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(42).finished_spans->at(0);
      REQUIRE(result->metrics.find("_sampling_priority_v1") != result->metrics.end());
      REQUIRE(result->metrics["_sampling_priority_v1"] == 1);
    }

    SECTION("spans with an existing sampling priority may not be given a new one at Finish") {
      std::istringstream ctx(R"({
            "trace_id": "100",
            "parent_id": "100",
            "sampling_priority": -1
          })");
      auto context = SpanContext::deserialize(logger, ctx);
      Span span{logger,     nullptr,
                buffer,     get_time,
                100,        100,
                0,          std::move(*static_cast<SpanContext*>(context.value().get())),
                get_time(), "",
                "",         "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(100).finished_spans->at(0);
      REQUIRE(result->metrics.find("_sampling_priority_v1") != result->metrics.end());
      REQUIRE(result->metrics["_sampling_priority_v1"] == -1);
    }
  }

  SECTION("rules sampling") {
    auto rules_sampler = std::make_shared<MockRulesSampler>();
    rules_sampler->sampling_priority =
        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
    rules_sampler->rule_rate = 0.42;
    rules_sampler->limiter_rate = 0.99;
    auto buffer = std::make_shared<MockBuffer>(rules_sampler);

    SECTION("spans are tagged with rules sampler rates") {
      Span span{logger,     nullptr, buffer, get_time,
                100,        100,     0,      SpanContext{logger, 100, 100, "", {}},
                get_time(), "",      "",     "",
                "",         ""};
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces().at(100).finished_spans->at(0);
      REQUIRE(result->metrics.find("_dd.rule_psr") != result->metrics.end());
      REQUIRE(result->metrics.find("_dd.limit_psr") != result->metrics.end());
      REQUIRE(result->metrics["_dd.rule_psr"] == 0.42);
      REQUIRE(result->metrics["_dd.limit_psr"] == 0.99);
    }
  }

  // Sampling decision maker
  SECTION("_dd.p.dm tag") {
    const auto sampler = std::make_shared<MockRulesSampler>();
    sampler->priority_rate = 1.0;
    TracerOptions options;
    options.service = "supersvc";
    const auto buffer = std::make_shared<MockBuffer>(sampler, options.service);
    const auto tracer = std::make_shared<Tracer>(options, buffer, getRealTime, getId);

    SECTION("is included in root span when extracted from context") {
      sampler->sampling_priority = nullptr;

      const std::string expected_decision_maker = "-" + std::to_string(4);
      std::string tags;
      appendTag(tags, "_dd.p.dm", expected_decision_maker);

      MockTextMapCarrier carrier;
      carrier.Set("x-datadog-tags", tags);
      carrier.Set("x-datadog-trace-id", "123");
      carrier.Set("x-datadog-parent-id", "456");

      const auto maybe_context = tracer->Extract(carrier);
      REQUIRE(maybe_context);
      const auto& context = *maybe_context;
      REQUIRE(context);

      const auto span =
          tracer->StartSpan("OperationMoonUnit", {opentracing::ChildOf(context.get())});
      REQUIRE(span);
      span->FinishWithOptions(finish_options);

      REQUIRE(buffer->traces().size() == 1);
      const auto& entry = *buffer->traces().begin();
      REQUIRE(entry.first == 123);

      const auto& trace = entry.second;
      REQUIRE(trace.finished_spans);
      REQUIRE(trace.finished_spans->size() == 1);

      const auto& maybe_span_data = *trace.finished_spans->begin();
      REQUIRE(maybe_span_data);
      const auto& span_data = *maybe_span_data;

      const auto found_decision_maker = span_data.meta.find("_dd.p.dm");
      REQUIRE(found_decision_maker != span_data.meta.end());

      REQUIRE(found_decision_maker->second == expected_decision_maker);
    }

    SECTION("is included in root span when a sampling decision is made") {
      // We won't extract any context this time, but will make a sampling
      // decision, and so expect a corresponding "_dd.p.dm" tag on the root
      // span.
      sampler->sampling_priority =
          std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
      sampler->sampling_mechanism = SamplingMechanism::Default;
      sampler->applied_rate = sampler->priority_rate;

      const auto span = tracer->StartSpan("OperationMoonUnit");
      REQUIRE(span);
      span->FinishWithOptions(finish_options);

      REQUIRE(buffer->traces().size() == 1);
      const auto& entry = *buffer->traces().begin();

      const auto& trace = entry.second;
      REQUIRE(trace.finished_spans);
      REQUIRE(trace.finished_spans->size() == 1);

      const auto& maybe_span_data = *trace.finished_spans->begin();
      REQUIRE(maybe_span_data);
      const auto& span_data = *maybe_span_data;

      const auto found_decision_maker = span_data.meta.find("_dd.p.dm");
      REQUIRE(found_decision_maker != span_data.meta.end());
      const std::string& decision_maker = found_decision_maker->second;

      const std::string expected_decision_maker =
          "-" + std::to_string(int(sampler->sampling_mechanism.get<SamplingMechanism>()));
      REQUIRE(decision_maker == expected_decision_maker);
    }
  }

  SECTION("_dd.propagation_error tag") {
    const auto sampler = std::make_shared<MockRulesSampler>();
    TracerOptions options;
    options.service = "supersvc";    // doesn't matter
    options.tags_header_size = 512;  // this matters
    const auto buffer =
        std::make_shared<MockBuffer>(sampler, options.service, options.tags_header_size);
    const auto tracer = std::make_shared<Tracer>(options, buffer, getRealTime, getId);

    SECTION("is included in root span when x-datadog-tags propagation failed") {
      SECTION("due to the serialized value being too large") {
        const std::string tags = "_dd.p.foo=" + std::string(1024, 'x');

        MockTextMapCarrier carrier;
        carrier.Set("x-datadog-tags", tags);
        carrier.Set("x-datadog-trace-id", "123");
        carrier.Set("x-datadog-parent-id", "456");

        const auto maybe_context = tracer->Extract(carrier);
        REQUIRE(maybe_context);
        const auto& context = *maybe_context;
        REQUIRE(context);

        const auto span =
            tracer->StartSpan("OperationMoonUnit", {opentracing::ChildOf(context.get())});
        REQUIRE(span);

        // Now inject the context.  x-datadog-tags will fail to serialize, due
        // to being oversized, and so when the trace is finished there will be
        // a corresponding error tag.
        std::ostringstream dummy;
        const auto rcode = tracer->Inject(span->context(), dummy);
        REQUIRE(rcode);

        span->FinishWithOptions(finish_options);

        REQUIRE(buffer->traces().size() == 1);
        const auto& entry = *buffer->traces().begin();
        REQUIRE(entry.first == 123);

        const auto& trace = entry.second;
        REQUIRE(trace.finished_spans);
        REQUIRE(trace.finished_spans->size() == 1);

        const auto& maybe_span_data = *trace.finished_spans->begin();
        REQUIRE(maybe_span_data);
        const auto& span_data = *maybe_span_data;

        // Here's the behavior we're testing for.
        const auto found_error_tag = span_data.meta.find("_dd.propagation_error");
        REQUIRE(found_error_tag != span_data.meta.end());
        REQUIRE(found_error_tag->second == "inject_max_size");
      }
    }
  }
}
