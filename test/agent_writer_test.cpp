#include "../src/agent_writer.h"
#include <datadog/version.h>
#include "../src/agent_writer.cpp"  // Otherwise the compiler won't generate AgentWriter for us.
#include "mocks.h"

#include <ctime>

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

Trace make_trace(std::initializer_list<TestSpanData> spans) {
  Trace trace{new std::vector<std::unique_ptr<SpanData>>{}};
  for (const TestSpanData& span : spans) {
    trace->emplace_back(std::unique_ptr<TestSpanData>{new TestSpanData{span}});
  }
  return trace;
}

TEST_CASE("writer") {
  SECTION("initializes handle correctly") {
    std::atomic<bool> handle_destructed{false};
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{&handle_destructed}};
    MockHandle* handle = handle_ptr.get();
    auto sampler = std::make_shared<RulesSampler>();
    struct InitializationTestCase {
      std::string host;
      uint32_t port;
      std::string url;
      std::unordered_map<CURLoption, std::string, EnumClassHash> expected_opts;
    };
    auto test_case = GENERATE(values<InitializationTestCase>({
        {"hostname", 1234, "", {{CURLOPT_URL, "http://hostname:1234/v0.4/traces"}}},
        {"hostname",
         1234,
         "http://override:5678",
         {{CURLOPT_URL, "http://override:5678/v0.4/traces"}}},
        {"", 0, "https://localhost:8126", {{CURLOPT_URL, "https://localhost:8126/v0.4/traces"}}},
        {"localhost",
         8126,
         "unix:///path/to/trace-agent.socket",
         {{CURLOPT_UNIX_SOCKET_PATH, "/path/to/trace-agent.socket"},
          {CURLOPT_URL, "http://localhost:8126/v0.4/traces"}}},
        {"localhost",
         8126,
         "/path/to/trace-agent.socket",
         {{CURLOPT_UNIX_SOCKET_PATH, "/path/to/trace-agent.socket"},
          {CURLOPT_URL, "http://localhost:8126/v0.4/traces"}}},
    }));
    test_case.expected_opts[CURLOPT_TIMEOUT_MS] = "2000";

    AgentWriter writer{std::move(handle_ptr), std::chrono::seconds(1), 100,           {},
                       test_case.host,        test_case.port,          test_case.url, sampler};

    REQUIRE(handle->options == test_case.expected_opts);
  }

  std::atomic<bool> handle_destructed{false};
  std::unique_ptr<MockHandle> handle_ptr{new MockHandle{&handle_destructed}};
  MockHandle* handle = handle_ptr.get();
  auto sampler = std::make_shared<MockRulesSampler>();
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
                     6319,
                     "",
                     sampler};

  SECTION("traces can be sent") {
    writer.write(make_trace(
        {TestSpanData{"web", "service", "resource", "service.name", 1, 1, 0, 69, 420, 0}}));
    writer.flush(std::chrono::seconds(10));

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
                                   {CURLOPT_URL, "http://hostname:6319/v0.4/traces"},
                                   {CURLOPT_TIMEOUT_MS, "2000"},
                                   {CURLOPT_POSTFIELDSIZE, "135"}});
    REQUIRE(handle->headers ==
            std::map<std::string, std::string>{
                {"Content-Type", "application/msgpack"},
                {"Datadog-Meta-Lang", "cpp"},
                {"Datadog-Meta-Tracer-Version", ::datadog::version::tracer_version},
                {"Datadog-Meta-Lang-Version", ::datadog::version::cpp_version},
                {"X-Datadog-Trace-Count", "1"}});
  }

  SECTION("responses are sent to sampler") {
    handle->response = "{\"rate_by_service\": {\"service:nginx,env:\": 0.5}}";
    writer.write(make_trace(
        {TestSpanData{"web", "service", "resource", "service.name", 1, 1, 0, 69, 420, 0}}));
    writer.flush(std::chrono::seconds(10));

    REQUIRE(sampler->config == "{\"service:nginx,env:\":0.5}");
  }

  SECTION("handle dodgy responses") {
    struct BadResponseTest {
      std::string response;
      std::string error;
    };

    auto bad_response_test_case = GENERATE(values<BadResponseTest>(
        {{"// Error at start, short body",
          "Unable to parse response from agent.\n"
          "Error was: [json.exception.parse_error.101] parse error at line 1, column 1: "
          "syntax error while parsing value - invalid literal; last read: '/'\n"
          "Error near: // Error at start, short body\n"},
         {"{\"lol\" // Error near start, error message should have truncated "
          "body. 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9",
          "Unable to parse response from agent.\n"
          "Error was: [json.exception.parse_error.101] parse error at line 1, column 8: syntax "
          "error while parsing object separator - invalid literal; last read: '\"lol\" /'; "
          "expected ':'\n"
          "Error near: {\"lol\" // Error near start, error message should h...\n"},
         {"{\"Error near the end, should be truncated. 0 1 2 3 4 5 6 7 8 9 \", oh noes",
          "Unable to parse response from agent.\n"
          "Error was: [json.exception.parse_error.101] parse error at line 1, column 65: syntax "
          "error while parsing object separator - unexpected ','; expected ':'\n"
          "Error near: ...d. 0 1 2 3 4 5 6 7 8 9 \", oh noes\n"},
         {"{\"Error in the middle, truncated from both ends\" lol 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 "
          "6 7 8 9",
          "Unable to parse response from agent.\n"
          "Error was: [json.exception.parse_error.101] parse error at line 1, column 50: syntax "
          "error while parsing object separator - invalid literal; last read: '\"Error in the "
          "middle, truncated from both ends\" l'; expected ':'\n"
          "Error near: ...uncated from both ends\" lol 0 1 2 3 4 5 6 7 8 9 0 ...\n"}}));

    handle->response = bad_response_test_case.response;
    writer.write(make_trace(
        {TestSpanData{"web", "service", "resource", "service.name", 1, 1, 0, 69, 420, 0}}));

    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush(std::chrono::seconds(10));
    std::cerr.rdbuf(stderr);  // Restore stderr.
    REQUIRE(error_message.str() == bad_response_test_case.error);
    REQUIRE(sampler->config == "");
  }

  SECTION("queue does not grow indefinitely") {
    for (uint64_t i = 0; i < 30; i++) {  // Only 25 actually get written.
      writer.write(make_trace(
          {TestSpanData{"service.name", "service", "resource", "web", 1, i, 0, 0, 69, 420}}));
    }
    writer.flush(std::chrono::seconds(10));
    auto traces = handle->getTraces();
    REQUIRE(traces->size() == 25);
  }

  SECTION("bad handle causes constructor to fail") {
    std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
    handle_ptr->rcode = CURLE_OPERATION_TIMEDOUT;
    REQUIRE_THROWS(AgentWriter{std::move(handle_ptr), only_send_traces_when_we_flush,
                               max_queued_traces, disable_retry, "hostname", 6319, "",
                               std::make_shared<RulesSampler>()});
  }

  SECTION("handle failure during post") {
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    // Redirect stderr so the test logs don't look like a failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush(std::chrono::seconds(10));  // Doesn't throw an error. That's the test!
    REQUIRE(error_message.str() == "Error setting agent request size: Timeout was reached\n");
    std::cerr.rdbuf(stderr);  // Restore stderr.
    // Dropped all spans.
    handle->rcode = CURLE_OK;
    REQUIRE(handle->getTraces()->size() == 0);
  }

  SECTION("handle failure during perform") {
    handle->perform_result = {CURLE_OPERATION_TIMEDOUT};
    handle->error = "error from libcurl";
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());
    writer.flush(std::chrono::seconds(10));
    REQUIRE(error_message.str() ==
            "Error sending traces to agent: Timeout was reached\nerror from libcurl\n");
    std::cerr.rdbuf(stderr);  // Restore stderr.
  }

  SECTION("responses are not sent to sampler if the conenction fails") {
    handle->response = "{\"rate_by_service\": {\"service:nginx,env:\": 0.5}}";
    handle->perform_result = {CURLE_OPERATION_TIMEDOUT};
    writer.write(make_trace(
        {TestSpanData{"web", "service", "resource", "service.name", 1, 1, 0, 69, 420, 0}}));
    writer.flush(std::chrono::seconds(10));

    REQUIRE(sampler->config == "");
  }

  SECTION("destructed/stopped writer does nothing when written to") {
    writer.stop();  // Normally called by destructor.
    // We know the worker thread has stopped because it is the unique owner of handle (the
    // pointer we keep for testing is leaked) and has destructed it.
    REQUIRE(handle_destructed);
    // Check that these don't crash (but neither will they do anything).
    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));
    writer.flush(std::chrono::seconds(10));
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
    writer.flush(std::chrono::seconds(10));
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
    auto write_interval = std::chrono::milliseconds(200);
    AgentWriter writer{std::move(handle_ptr),
                       write_interval,
                       max_queued_traces,
                       disable_retry,
                       "hostname",
                       6319,
                       "",
                       std::make_shared<RulesSampler>()};
    // Send 7 traces at 1 trace per second. Since the write period is 2s, there should be 4
    // different writes. We don't count the number of writes because that could flake, but we do
    // check that all 7 traces are written, implicitly testing that multiple writes happen.
    std::thread sender([&]() {
      for (uint64_t i = 1; i <= 7; i++) {
        writer.write(make_trace(
            {TestSpanData{"web", "service", "resource", "service.name", i, 1, 0, 69, 420, 0}}));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    std::vector<std::chrono::milliseconds> retry_periods{std::chrono::milliseconds(50),
                                                         std::chrono::milliseconds(99)};
    AgentWriter writer{std::move(handle_ptr),
                       only_send_traces_when_we_flush,
                       max_queued_traces,
                       retry_periods,
                       "hostname",
                       6319,
                       "",
                       std::make_shared<RulesSampler>()};
    // Redirect cerr, so the the terminal output doesn't imply failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());

    writer.write(make_trace(
        {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));

    SECTION("will retry") {
      handle->perform_result = {CURLE_OPERATION_TIMEDOUT, CURLE_OK};
      writer.flush(std::chrono::seconds(10));
      REQUIRE(handle->perform_call_count == 2);
    }

    SECTION("will eventually give up") {
      handle->perform_result = {CURLE_OPERATION_TIMEDOUT};
      writer.flush(std::chrono::seconds(10));
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
      writer.flush(std::chrono::seconds(10));
      REQUIRE(handle->headers ==
              std::map<std::string, std::string>{
                  {"Content-Type", "application/msgpack"},
                  {"Datadog-Meta-Lang", "cpp"},
                  {"Datadog-Meta-Tracer-Version", ::datadog::version::tracer_version},
                  {"Datadog-Meta-Lang-Version", ::datadog::version::cpp_version},
                  {"X-Datadog-Trace-Count", "3"}});
    }
  }
}

TEST_CASE("flush") {
  std::unique_ptr<MockHandle> handle_ptr{new MockHandle{}};
  MockHandle* handle = handle_ptr.get();
  std::vector<std::chrono::milliseconds> retry_periods{std::chrono::seconds(60)};
  AgentWriter writer{std::move(handle_ptr),
                     std::chrono::seconds(3600),
                     max_queued_traces,
                     retry_periods,
                     "hostname",
                     6319,
                     "",
                     std::make_shared<RulesSampler>()};
  // Redirect cerr, so the the terminal output doesn't imply failure.
  std::stringstream error_message;
  std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());

  writer.write(make_trace(
      {TestSpanData{"web", "service", "service.name", "resource", 1, 1, 0, 69, 420, 0}}));

  SECTION("will time out") {
    // MockHandle doesn't actually block/wait, but we can make the AgentWriter wait for 60
    // seconds (see retry_periods above) to retry. We make sure that flush() times out before
    // that.
    handle->perform_result = {CURLE_OPERATION_TIMEDOUT};
    steady_clock::time_point start = steady_clock::now();
    writer.flush(std::chrono::milliseconds(250));
    steady_clock::duration wait_time = steady_clock::now() - start;
    // Since this involves timing, it is technically possible for it to flake. I think it's
    // unlikely since 30s >>> 0.25s
    REQUIRE(wait_time < (retry_periods[0] / 2));
  }

  std::cerr.rdbuf(stderr);  // Restore stderr.
}
