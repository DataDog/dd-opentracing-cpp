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
  auto writer = new MockWriter();
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  IdProvider get_id = [&id]() { return id++; };        // Mock ID provider.
  const ot::StartSpanOptions span_options;

  SECTION("receives id") {
    Span span{nullptr,     std::shared_ptr<Writer<Span>>{writer}, get_time, get_id, "", "", "", "",
              span_options};
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    REQUIRE(writer->spans.size() == 1);
    REQUIRE(writer->spans[0].span_id == 100);
    REQUIRE(writer->spans[0].trace_id == 100);
    REQUIRE(writer->spans[0].parent_id == 0);
  }

  SECTION("timed correctly") {
    Span span{nullptr,     std::shared_ptr<Writer<Span>>{writer}, get_time, get_id, "", "", "", "",
              span_options};
    advanceSeconds(time, 10);
    const ot::FinishSpanOptions finish_options;
    span.FinishWithOptions(finish_options);

    REQUIRE(writer->spans.size() == 1);
    REQUIRE(writer->spans[0].duration == 10000000000);
  }

  SECTION("finishes once") {
    Span span{nullptr,     std::shared_ptr<Writer<Span>>{writer}, get_time, get_id, "", "", "", "",
              span_options};
    const ot::FinishSpanOptions finish_options;
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&]() { span.FinishWithOptions(finish_options); });
    }
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    REQUIRE(writer->spans.size() == 1);
  }

  SECTION("parses tags") {
    Span span{nullptr,     std::shared_ptr<Writer<Span>>{writer}, get_time, get_id, "", "", "", "",
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

    REQUIRE(writer->spans.size() == 1);
    // Check "map" seperately, because JSON key order is non-deterministic therefore we can't do
    // simple string matching.
    REQUIRE(json::parse(writer->spans[0].meta["map"]) ==
            json::parse(R"({"a":"1","b":2,"c":{"nesting":true}})"));
    writer->spans[0].meta.erase("map");
    // Check the rest.
    REQUIRE(writer->spans[0].meta == std::unordered_map<std::string, std::string>{
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
}
