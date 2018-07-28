#include "../src/writer.h"
#include "../src/writer.cpp"  // Otherwise the compiler won't generate AgentWriter for us.
#include "mocks.h"
#include "version_number.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

Trace make_trace(std::initializer_list<TestSpanData> spans) {
  Trace trace{new std::vector<std::unique_ptr<SpanData>>{}};
  for (const TestSpanData& span : spans) {
    trace->emplace_back(std::move(std::unique_ptr<TestSpanData>{new TestSpanData{span}}));
  }
  return std::move(trace);
}

TEST_CASE("writer") {
  std::atomic<bool> handle_destructed{false};
  std::unique_ptr<MockHandle> handle_ptr{new MockHandle{&handle_destructed}};
  MockHandle* handle = handle_ptr.get();
  // I mean, it *can* technically still flake, but if this test takes an hour we've got bigger
  // problems.
  auto only_send_traces_when_we_flush = std::chrono::seconds(3600);
  size_t max_queued_traces = 25;
  std::vector<std::chrono::milliseconds> disable_retry;
  AgentWriter writer{std::move(handle_ptr),
                     only_send_traces_when_we_flush,
                     max_queued_traces,
                     disable_retry,
                     "hostname",
                     6319};

  SECTION("initilises handle correctly") {
    REQUIRE(handle->options ==
            std::unordered_map<CURLoption, std::string, EnumClassHash>{
                {CURLOPT_URL, "http://hostname:6319/v0.3/traces"}, {CURLOPT_TIMEOUT_MS, "2000"}});
  }

  SECTION("traces can be sent") {
    writer.write(make_trace(
        {TestSpanData{"web", "service", "resource", "service.name", 1, 1, 0, 69, 420, 0}}));
    writer.flush();

    // Check span body.
    auto traces = handle->getTraces();
    REQUIRE(traces->size() == 1);
    REQUIRE((*traces)[0].size() == 1);
    REQUIRE((*traces)[0][0].name == "service.name");
    REQUIRE((*traces)[0][0].service == "service");
    REQUIRE((*traces)[0][0].resource == "resource");
    REQUIRE((*traces)[0][0].type == "web");
    REQUIRE((*traces)[0][0].span_id == 1);
    REQUIRE((*traces)[0][0].trace_id == 1);
    REQUIRE((*traces)[0][0].parent_id == 0);
    REQUIRE((*traces)[0][0].error == 0);
    REQUIRE((*traces)[0][0].start == 69);
    REQUIRE((*traces)[0][0].duration == 420);
    // Check general Curl connection config.
    // Remove postdata first, since it's ugly to print and we just tested it above.
    handle->options.erase(CURLOPT_POSTFIELDS);
    REQUIRE(handle->options == std::unordered_map<CURLoption, std::string, EnumClassHash>{
                                   {CURLOPT_URL, "http://hostname:6319/v0.3/traces"},
                                   {CURLOPT_TIMEOUT_MS, "2000"},
                                   {CURLOPT_POSTFIELDSIZE, "126"}});
    REQUIRE(handle->headers == std::map<std::string, std::string>{
                                   {"Content-Type", "application/msgpack"},
                                   {"Datadog-Meta-Lang", "cpp"},
                                   {"Datadog-Meta-Tracer-Version", config::tracer_version},
                                   {"X-Datadog-Trace-Count", "1"}});
  }

  SECTION("queue does not grow indefinitely") {
    for (uint64_t i = 0; i < 30; i++) {  // Only 25 actually get written.
      writer.write(make_trace(
          {TestSpanData{"service.name", "service", "resource", "web", 1, i, 0, 0, 69, 420}}));
    }
    writer.flush();
    auto traces = handle->getTraces();
    REQUIRE(traces->size() == 25);
  }

  SECTION("bad handle causes constructor to fail") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    handle_ptr->rcode = CURLE_OPERATION_TIMEDOUT;
    REQUIRE_THROWS(AgentWriter{std::move(handle_ptr), only_send_traces_when_we_flush,
                               max_queued_traces, disable_retry, "hostname", 6319});
  }

  SECTION("handle failure during post") {
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    // Redirect stderr so the test logs don't look like a failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush();  // Doesn't throw an error. That's the test!
    REQUIRE(error_message.str() == "Error setting agent request size: Timeout was reached\n");
    std::cerr.rdbuf(stderr);  // Restore stderr.
    // Dropped all spans.
    handle->rcode = CURLE_OK;
    REQUIRE(handle->getTraces()->size() == 0);
  }

  SECTION("handle failure during perform") {
    handle->perform_result = std::vector<CURLcode>{CURLE_OPERATION_TIMEDOUT};
    handle->error = "error from libcurl";
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush();
    REQUIRE(error_message.str() ==
            "Error sending traces to agent: Timeout was reached\nerror from libcurl\n");
    std::cerr.rdbuf(stderr);  // Restore stderr.
  }

  SECTION("destructed/stopped writer does nothing when written to") {
    writer.stop();  // Normally called by destructor.
    // We know the worker thread has stopped because it is the unique owner of handle (the
    // pointer we keep for testing is leaked) and has destructed it.
    REQUIRE(handle_destructed);
    // Check that these don't crash (but neither will they do anything).
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    writer.flush();
  }

  SECTION("there can be multiple threads sending Spans") {
    // Write concurrently.
    std::vector<std::thread> senders;
    for (uint64_t i = 1; i <= 4; i++) {
      senders.emplace_back(
          [&](uint64_t trace_id) {
            writer.write(make_trace({TestSpanData{"web", "service", "resource", "service.name",
                                                  trace_id, 1, 0, 69, 420, 0},
                                     TestSpanData{"web", "service", "resource", "service.name",
                                                  trace_id, 2, 0, 69, 420, 0},
                                     TestSpanData{"web", "service", "resource", "service.name",
                                                  trace_id, 3, 0, 69, 420, 0},
                                     TestSpanData{"web", "service", "resource", "service.name",
                                                  trace_id, 4, 0, 69, 420, 0},
                                     TestSpanData{"web", "service", "resource", "service.name",
                                                  trace_id, 5, 0, 69, 420, 0}}));
          },
          i);
    }
    for (std::thread& sender : senders) {
      sender.join();
    }
    writer.flush();
    // Now check.
    auto traces = handle->getTraces();
    REQUIRE(traces->size() == 4);
    // Make sure all senders sent their Span.
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> seen_ids;
    for (auto trace : (*traces)) {
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
                                                                       {2, {1, 2, 3, 4, 5}},
                                                                       {3, {1, 2, 3, 4, 5}},
                                                                       {4, {1, 2, 3, 4, 5}}});
  }

  SECTION("writes happen periodically") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    MockHandle* handle = handle_ptr.get();
    auto write_interval = std::chrono::seconds(2);
    AgentWriter writer{std::move(handle_ptr), write_interval, max_queued_traces,
                       disable_retry,         "hostname",     6319};
    // Send 7 traces at 1 trace per second. Since the write period is 2s, there should be 4
    // different writes. We don't count the number of writes because that could flake, but we do
    // check that all 7 traces are written, implicitly testing that multiple writes happen.
    std::thread sender([&]() {
      for (uint64_t i = 1; i <= 7; i++) {
        writer.write(make_trace(
            {TestSpanData{"web", "service", "resource", "service.name", i, 1, 0, 69, 420, 0}}));
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });
    // Wait until data is written.
    std::unordered_set<uint64_t> trace_ids;
    while (trace_ids.size() < 7) {
      handle->waitUntilPerformIsCalled();
      auto data = handle->getTraces();
      std::transform((*data).begin(), (*data).end(), std::inserter(trace_ids, trace_ids.begin()),
                     [](std::vector<TestSpanData>& trace) -> uint64_t {
                       REQUIRE(trace.size() == 1);
                       return trace[0].trace_id;
                     });
    }
    // We got all 7 traces without calling flush ourselves.
    REQUIRE(trace_ids == std::unordered_set<uint64_t>{1, 2, 3, 4, 5, 6, 7});
    sender.join();
  }

  SECTION("failed agent comms") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    MockHandle* handle = handle_ptr.get();
    std::vector<std::chrono::milliseconds> retry_periods{std::chrono::milliseconds(500),
                                                         std::chrono::milliseconds(2500)};
    AgentWriter writer{std::move(handle_ptr),
                       only_send_traces_when_we_flush,
                       max_queued_traces,
                       retry_periods,
                       "hostname",
                       6319};
    // Redirect cerr, so the the terminal output doesn't imply failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());

    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));

    SECTION("will retry") {
      handle->perform_result = std::vector<CURLcode>{CURLE_OPERATION_TIMEDOUT, CURLE_OK};
      writer.flush();
      REQUIRE(handle->perform_call_count == 2);
    }

    SECTION("will eventually give up") {
      handle->perform_result = std::vector<CURLcode>{CURLE_OPERATION_TIMEDOUT};
      writer.flush();
      REQUIRE(handle->perform_call_count == 3);  // Once originally, and two retries.
    }

    std::cerr.rdbuf(stderr);  // Restore stderr.
  }

  SECTION("multiple requests don't append headers") {
    // Regression test for an issue where CURL only allows appending headers, not changing them,
    // therefore leading to extraneous headers.
    for (int i = 0; i < 5; i++) {
      writer.write(make_trace(
          {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
      writer.write(make_trace(
          {TestSpanData{"web", "service", "resource", "service.name", 2, 1, 1, 69, 420, 0}}));
      writer.write(make_trace(
          {TestSpanData{"web", "service", "resource", "service.name", 3, 1, 1, 69, 420, 0}}));
      writer.flush();
      REQUIRE(handle->headers == std::map<std::string, std::string>{
                                     {"Content-Type", "application/msgpack"},
                                     {"Datadog-Meta-Lang", "cpp"},
                                     {"Datadog-Meta-Tracer-Version", config::tracer_version},
                                     {"X-Datadog-Trace-Count", "3"}});
    }
  }
}
