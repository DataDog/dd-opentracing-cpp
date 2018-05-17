#include "../src/span.h"
#include "mocks.h"

#include <ctime>
#include <nlohmann/json.hpp>
#include <thread>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;
using json = nlohmann::json;

TEST_CASE("span") {
  int id = 100;                        // Starting span id.
  std::tm start{0, 0, 0, 12, 2, 107};  // Starting calendar time 2007-03-12 00:00:00
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  auto buffer = new MockBuffer();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  const ot::StartSpanOptions span_options;

  SECTION("receives id") {
    Span span{
        nullptr,     std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, "", "", "", "",
        span_options};
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result.span_id == 100);
    REQUIRE(result.trace_id == 100);
    REQUIRE(result.parent_id == 0);
  }

  SECTION("registers with SpanBuffer") {
    Span span{
        nullptr,     std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, "", "", "", "",
        span_options};
    REQUIRE(buffer->traces.size() == 1);
    REQUIRE(buffer->traces.find(100) != buffer->traces.end());
    REQUIRE(buffer->traces[100].finished_spans->size() == 0);
    REQUIRE(buffer->traces[100].all_spans.size() == 1);
  }

  SECTION("timed correctly") {
    Span span{
        nullptr,     std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, "", "", "", "",
        span_options};
    advanceSeconds(time, 10);
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces[100].finished_spans->at(0);
    REQUIRE(result.duration == 10000000000);
  }

  SECTION("finishes once") {
    Span span{
        nullptr,     std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, "", "", "", "",
        span_options};
    const ot::FinishSpanOptions finish_options;
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&]() { span.FinishWithOptions(finish_options); });
    }
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    REQUIRE(buffer->traces.size() == 1);
    REQUIRE(buffer->traces.find(100) != buffer->traces.end());
    REQUIRE(buffer->traces[100].finished_spans->size() == 1);
  }

  SECTION("handles tags") {
    Span span{
        nullptr,     std::shared_ptr<SpanBuffer<Span>>{buffer}, get_time, get_id, "", "", "", "",
        span_options};

    span.SetTag("bool", true);
    span.SetTag("double", 6.283185);
    span.SetTag("int64_t", -69);
    span.SetTag("uint64_t", 420);
    span.SetTag("std::string", std::string("hi there"));
    span.SetTag("nullptr", nullptr);
    span.SetTag("char*", "hi there");
    span.SetTag("list", std::vector<ot::Value>{"hi", 420, true});
    span.SetTag("map", std::unordered_map<std::string, ot::Value>{
                           {"a", "1"},
                           {"b", 2},
                           {"c", std::unordered_map<std::string, ot::Value>{{"nesting", true}}}});

    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces[100].finished_spans->at(0);
    // Check "map" seperately, because JSON key order is non-deterministic therefore we can't do
    // simple string matching.
    REQUIRE(json::parse(result.meta["map"]) ==
            json::parse(R"({"a":"1","b":2,"c":{"nesting":true}})"));
    result.meta.erase("map");
    // Check the rest.
    REQUIRE(result.meta == std::unordered_map<std::string, std::string>{
                               {"bool", "true"},
                               {"double", "6.283185"},
                               {"int64_t", "-69"},
                               {"uint64_t", "420"},
                               {"std::string", "hi there"},
                               {"nullptr", "nullptr"},
                               {"char*", "hi there"},
                               {"list", "[\"hi\",420,true]"},
                           });
  }

  SECTION("maps datadog tags to span data") {
    Span span{nullptr,
              std::shared_ptr<SpanBuffer<Span>>{buffer},
              get_time,
              get_id,
              "original service",
              "original type",
              "original span name",
              "original resource",
              span_options};
    span.SetTag("span.type", "new type");
    span.SetTag("resource.name", "new resource");
    span.SetTag("service.name", "new service");
    span.SetTag("tag with no special meaning", "ayy lmao");

    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    auto& result = buffer->traces[100].finished_spans->at(0);
    // Datadog special tags aren't kept, they just set the Span values.
    REQUIRE(result.meta == std::unordered_map<std::string, std::string>{
                               {"tag with no special meaning", "ayy lmao"}});
    REQUIRE(result.name == "original span name");
    REQUIRE(result.resource == "new resource");
    REQUIRE(result.service == "new service");
    REQUIRE(result.type == "new type");
  }

  SECTION("OpenTracing operation name works") {
    Span span{nullptr,
              std::shared_ptr<SpanBuffer<Span>>{buffer},
              get_time,
              get_id,
              "original service",
              "original type",
              "original span name",
              "original resource",
              span_options};
    span.SetOperationName("operation name");

    SECTION("sets resource and span name") {
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces[100].finished_spans->at(0);
      REQUIRE(result.name == "operation name");
      REQUIRE(result.resource == "operation name");
    }

    SECTION("sets resource, but can be overridden by Datadog tag") {
      span.SetTag("resource.name", "resource tag override");
      const ot::FinishSpanOptions finish_options;
      span.FinishWithOptions(finish_options);

      auto& result = buffer->traces[100].finished_spans->at(0);
      REQUIRE(result.name == "operation name");
      REQUIRE(result.resource == "resource tag override");
    }
  }
}
