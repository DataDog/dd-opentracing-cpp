#include "../src/writer.h"
#include "../src/writer.cpp"  // Otherwise the compiler won't generate AgentWriter<SpanInfo> for us.
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("writer") {
  std::atomic<bool> handle_destructed{false};
  std::unique_ptr<MockHandle> handle_ptr{new MockHandle{&handle_destructed}};
  MockHandle* handle = handle_ptr.get();
  // I mean, it *can* technically still flake, but if this test takes an hour we've got bigger
  // problems.
  auto only_send_spans_when_we_flush = std::chrono::seconds(3600);
  std::vector<std::chrono::milliseconds> disable_retry;
  AgentWriter<SpanInfo> writer{std::move(handle_ptr), "v0.1.0",   only_send_spans_when_we_flush,
                               disable_retry,         "hostname", 6319};

  SECTION("initilises handle correctly") {
    REQUIRE(handle->options ==
            std::unordered_map<CURLoption, std::string, EnumClassHash>{
                {CURLOPT_URL, "http://hostname:6319/v0.3/traces"}, {CURLOPT_TIMEOUT_MS, "2000"}});
    REQUIRE(handle->headers == std::list<std::string>{"Content-Type: application/msgpack",
                                                      "Datadog-Meta-Lang: cpp",
                                                      "Datadog-Meta-Tracer-Version: v0.1.0"});
  }

  SECTION("spans can be sent") {
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    writer.flush();

    // Check span body.
    auto spans = handle->getSpans();
    REQUIRE(spans->size() == 1);
    REQUIRE((*spans)[0].size() == 1);
    REQUIRE((*spans)[0][0].name == "service.name");
    REQUIRE((*spans)[0][0].service == "service");
    REQUIRE((*spans)[0][0].resource == "resource");
    REQUIRE((*spans)[0][0].type == "web");
    REQUIRE((*spans)[0][0].span_id == 1);
    REQUIRE((*spans)[0][0].trace_id == 1);
    REQUIRE((*spans)[0][0].parent_id == 0);
    REQUIRE((*spans)[0][0].error == 0);
    REQUIRE((*spans)[0][0].start == 69);
    REQUIRE((*spans)[0][0].duration == 420);
    // Check general Curl connection config.
    // Remove postdata first, since it's ugly to print and we just tested it above.
    handle->options.erase(CURLOPT_POSTFIELDS);
    REQUIRE(handle->options == std::unordered_map<CURLoption, std::string, EnumClassHash>{
                                   {CURLOPT_URL, "http://hostname:6319/v0.3/traces"},
                                   {CURLOPT_TIMEOUT_MS, "2000"},
                                   {CURLOPT_POSTFIELDSIZE, "120"}});
    REQUIRE(handle->headers == std::list<std::string>{"Content-Type: application/msgpack",
                                                      "Datadog-Meta-Lang: cpp",
                                                      "Datadog-Meta-Tracer-Version: v0.1.0",
                                                      "X-Datadog-Trace-Count: 1"});
  }

  SECTION("bad handle causes constructor to fail") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    handle_ptr->rcode = CURLE_OPERATION_TIMEDOUT;
    REQUIRE_THROWS(AgentWriter<SpanInfo>{std::move(handle_ptr), "v0.1.0",
                                         only_send_spans_when_we_flush, disable_retry, "hostname",
                                         6319});
  }

  SECTION("handle failure during perform/sending") {
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    // Redirect stderr so the test logs don't look like a failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush();  // Doesn't throw an error. That's the test!
    REQUIRE(error_message.str() ==
            "Error setting agent communication headers: Timeout was reached\n");
    std::cerr.rdbuf(stderr);  // Restore stderr.
    // Dropped all spans.
    handle->rcode = CURLE_OK;
    REQUIRE(handle->getSpans()->size() == 0);
  }

  SECTION("destructed/stopped writer does nothing when written to") {
    writer.stop();  // Normally called by destructor.
    // We know the worker thread has stopped because it is the unique owner of handle (the pointer
    // we keep for testing is leaked) and has destructed it.
    REQUIRE(handle_destructed);
    // Check that these don't crash (but neither will they do anything).
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    writer.flush();
  }

  SECTION("there can be multiple threads sending Spans") {
    // Write concurrently.
    std::vector<std::thread> senders;
    for (uint64_t i = 1; i <= 20; i++) {
      senders.emplace_back(
          [&](uint64_t id) {
            uint64_t trace_id = (id - 1) / 5 + 1;  // 1, 2, 3, 4
            writer.write(std::move(SpanInfo{"service.name", "service", "resource", "web", id,
                                            trace_id, 0, 0, 69, 420}));
          },
          i);
    }
    for (std::thread& sender : senders) {
      sender.join();
    }
    writer.flush();
    // Now check.
    auto spans = handle->getSpans();
    REQUIRE(spans->size() == 4);
    // Make sure all senders sent their Span.
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> seen_ids;
    for (auto trace : (*spans)) {
      for (auto span : trace) {
        seen_ids[span.trace_id].insert(span.span_id);
        REQUIRE(span.name == "service.name");
        REQUIRE(span.service == "service");
        REQUIRE(span.resource == "resource");
        REQUIRE(span.type == "web");
        REQUIRE(span.parent_id == 0);
        REQUIRE(span.error == 0);
        REQUIRE(span.start == 69);
        REQUIRE(span.duration == 420);
      }
    }
    REQUIRE(seen_ids ==
            std::unordered_map<uint64_t, std::unordered_set<uint64_t>>{{1, {1, 2, 3, 4, 5}},
                                                                       {2, {6, 7, 8, 9, 10}},
                                                                       {3, {11, 12, 13, 14, 15}},
                                                                       {4, {16, 17, 18, 19, 20}}});
  }

  SECTION("writes happen periodically") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    MockHandle* handle = handle_ptr.get();
    auto write_interval = std::chrono::seconds(2);
    AgentWriter<SpanInfo> writer{std::move(handle_ptr), "v0.1.0",   write_interval,
                                 disable_retry,         "hostname", 6319};
    // Send 7 spans at 1 Span per second. Since the write period is 2s, there should be 4 different
    // writes. We don't count the number of writes because that could flake, but we do check that
    // all 7 Spans are written, implicitly testing that multiple writes happen.
    std::thread sender([&]() {
      for (uint64_t i = 1; i <= 7; i++) {
        writer.write(std::move(
            SpanInfo{"service.name", "service", "resource", "web", i, 1, 0, 0, 69, 420}));
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });
    // Wait until data is written.
    std::unordered_set<uint64_t> span_ids;
    while (span_ids.size() < 7) {
      handle->waitUntilDataWritten();
      auto data = handle->getSpans();
      REQUIRE(data->size() == 1);
      std::transform((*data)[0].begin(), (*data)[0].end(),
                     std::inserter(span_ids, span_ids.begin()),
                     [](SpanInfo& span) -> uint64_t { return span.span_id; });
    }
    // We got all 7 spans without calling flush ourselves.
    REQUIRE(span_ids == std::unordered_set<uint64_t>{1, 2, 3, 4, 5, 6, 7});
    sender.join();
  }

  // FIXME: Tests for: timeout, retry.
}
