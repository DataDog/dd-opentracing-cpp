#include "../src/logger.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("logger") {
  struct LoggerTest {
    std::shared_ptr<Logger> logger;
    bool trace = false;
    std::string debug_no_id;
    std::string debug_trace_id;
    std::string debug_trace_span_ids;
    std::string trace_no_id;
    std::string trace_trace_id;
    std::string trace_trace_span_ids;
  };

  bool called = false;
  std::string message;

  auto log_func = [&](LogLevel, ot::string_view msg) {
    called = true;
    message = msg;
  };

  auto reset = [&]() {
    called = false;
    message = "";
  };

  auto test_case = GENERATE_REF(values<LoggerTest>({
      {std::make_shared<StandardLogger>(log_func), false, "test debug message",
       "[trace_id: 42] test debug message", "[trace_id: 42, span_id: 99] test debug message", "",
       "", ""},
      {std::make_shared<VerboseLogger>(log_func), true, "test debug message",
       "[trace_id: 42] test debug message", "[trace_id: 42, span_id: 99] test debug message",
       "test trace message", "[trace_id: 42] test trace message",
       "[trace_id: 42, span_id: 99] test trace message"},
  }));

  // Message logged with debug log level.
  test_case.logger->Log(LogLevel::debug, "test debug message");
  REQUIRE(called);
  REQUIRE(message == test_case.debug_no_id);
  reset();

  test_case.logger->Log(LogLevel::debug, 42, "test debug message");
  REQUIRE(called);
  REQUIRE(message == test_case.debug_trace_id);
  reset();

  test_case.logger->Log(LogLevel::debug, 42, 99, "test debug message");
  REQUIRE(called);
  REQUIRE(message == test_case.debug_trace_span_ids);
  reset();

  // Message only logged with trace level if using verbose logger.
  test_case.logger->Trace("test trace message");
  if (test_case.trace) {
    REQUIRE(called);
    REQUIRE(message == test_case.trace_no_id);
  } else {
    REQUIRE(!called);
  }
  reset();

  test_case.logger->Trace(42, "test trace message");
  if (test_case.trace) {
    REQUIRE(called);
    REQUIRE(message == test_case.trace_trace_id);
  } else {
    REQUIRE(!called);
  }
  reset();

  test_case.logger->Trace(42, 99, "test trace message");
  if (test_case.trace) {
    REQUIRE(called);
    REQUIRE(message == test_case.trace_trace_span_ids);
  } else {
    REQUIRE(!called);
  }
  reset();
}
