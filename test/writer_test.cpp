#include "../src/writer.h"
#include "../src/writer.cpp"  // Otherwise the compiler won't generate AgentWriter<SpanInfo> for us.
#include "mocks.h"

#include <ctime>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("writer") {
  std::shared_ptr<MockHandle> handle{new MockHandle{}};
  AgentWriter<SpanInfo> writer{handle, "v0.1.0", "hostname", 6319};

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
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    REQUIRE_THROWS(AgentWriter<SpanInfo>{handle, "v0.1.0", "hostname", 6319});
  }

  SECTION("handle failure during perform/sending") {
    handle->rcode = CURLE_OPERATION_TIMEDOUT;
    // Doesn't throw an error.
    writer.write(
        std::move(SpanInfo{"service.name", "service", "resource", "web", 1, 1, 0, 0, 69, 420}));
    // Dropped all spans.
    handle->rcode = CURLE_OK;
    writer.flush();
    REQUIRE((*handle->getSpans())[0].size() == 0);
  }
}
