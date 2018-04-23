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
  AgentWriter<SpanInfo> writer{std::move(handle_ptr), "v0.1.0", only_send_spans_when_we_flush,
                               "hostname", 6319};

  SECTION("initilises handle correctly") {
    REQUIRE(handle->options == std::unordered_map<CURLoption, std::string, EnumClassHash>{
                                   {CURLOPT_URL, "https://hostname:6319/v0.3/traces"}});
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
                                   {CURLOPT_URL, "https://hostname:6319/v0.3/traces"},
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
                                         only_send_spans_when_we_flush, "hostname", 6319});
  }

  SECTION("handle failure during perform/sending") {
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    // Doesn't throw an error.
    writer.flush();
    // Dropped all spans.
    handle->rcode = CURLE_OK;
    REQUIRE(handle->getSpans()->size() == 0);
  }

  SECTION("destructed/stopped writer does nothing when written to") {
    writer.stop();  // Normally called by destructor.
    // We know the worker thread has stopped because it is the unique owner of handle (the pointer
    // we keep for testing is leaked) and has destructed it.
    REQUIRE(handle_destructed);
    // Check that these don't crash (but niether will they do anything).
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    writer.flush();
  }
}
