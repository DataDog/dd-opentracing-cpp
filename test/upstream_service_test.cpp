// This test covers the "upstream service" formatting functions declared in
// `upstream_service.h`.

#include "../src/upstream_service.h"

#include <catch2/catch.hpp>
#include <cmath>
#include <ostream>
#include <stdexcept>
#include <string>

using namespace datadog::opentracing;

namespace datadog {
namespace opentracing {

// catch2 will print values in failed assertions, but only if those values have a corresponding
// `operator<<`.
std::ostream& operator<<(std::ostream& stream, const UpstreamService& value) {
  const std::vector<UpstreamService> wrapper = {value};
  return stream << serializeUpstreamServices(wrapper);
}

}  // namespace opentracing
}  // namespace datadog

TEST_CASE("sampling rate formatting") {
  struct TestCase {
    double input;
    std::string output;
  };

  // Always four digits after the decimal, and at least one before.
  // The number is rounded _up_ to the fourth decimal place.
  // If the input is NaN, then the output is an empty string.
  auto test_case = GENERATE(values<TestCase>({{0.0123456789, "0.0124"},
                                              {0, "0.0000"},
                                              {0.123, "0.1230"},
                                              {0.12340, "0.1234"},
                                              {0.123409, "0.1235"},
                                              {-1, "-1.0000"},
                                              {1337, "1337.0000"},
                                              {std::nan(""), ""}}));

  std::string result;
  appendSamplingRate(result, test_case.input);
  REQUIRE(result == test_case.output);
}

TEST_CASE("serializeUpstreamServices/deserializeUpstreamServices") {
  struct TestCase {
    std::vector<UpstreamService> decoded;
    std::string encoded;
  };

  // clang-format off
  auto test_case = GENERATE(values<TestCase>({
    { // empty
      {},
      ""
    },
    { // just one service
      {{"mysvc", SamplingPriority::UserKeep, int(SamplingMechanism::Rule), 0.01234, {"extra", "junk"}}},
      "bXlzdmM|2|3|0.0124|extra|junk"
    },
    { // two services
      {{"yoursvc", SamplingPriority::SamplerDrop, 1337, 1.0, {}},
       {"mysvc", SamplingPriority::UserKeep, int(SamplingMechanism::Rule), 0.01234, {"extra", "junk"}}},
      "eW91cnN2Yw|0|1337|1.0000;bXlzdmM|2|3|0.0124|extra|junk"
    },
    { // example based on internal design document
      {{"mcnulty-web", SamplingPriority::SamplerDrop, int(SamplingMechanism::AgentRate), std::nan(""), {}},
       {"trace-stats-query", SamplingPriority::UserKeep, int(SamplingMechanism::Manual), std::nan(""), {"foo"}}},
      "bWNudWx0eS13ZWI|0|1|;dHJhY2Utc3RhdHMtcXVlcnk|2|4||foo"
    }
  }));
  // clang-format on

  REQUIRE(serializeUpstreamServices(test_case.decoded) == test_case.encoded);
  REQUIRE(deserializeUpstreamServices(test_case.encoded) == test_case.decoded);
}

// This test case aims to cover every explicit `throw` under `deserializeUpstreamServices`.
TEST_CASE("parsing fails") {
  // I don't use `GENERATE` here, becuase the diagnostic printed when
  // `REQUIRE_THROWS_AS` fails does not expand the test case value

  // invalid base64 in service name
  REQUIRE_THROWS_AS(deserializeUpstreamServices("{curlies are invalid base64}"),
                    std::invalid_argument);
  // missing sampling priority field
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0|1|0.1;"), std::invalid_argument);
  // bogus sampling priority
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|totally not an integer"),
                    std::invalid_argument);
  // sampling priority doesn't fit into an integer
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|9999999999"), std::invalid_argument);
  // unknown sampling priority integer value
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|1337"), std::invalid_argument);
  // missing sampling mechanism field
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0"), std::invalid_argument);
  // bogus sampling mechanism
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0|also not an integer"),
                    std::invalid_argument);
  // sampling mechanism doesn't fit into an integer
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0|9999999999"), std::invalid_argument);
  // missing sampling rate field
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0|1"), std::invalid_argument);
  // bogus sampling rate
  REQUIRE_THROWS_AS(deserializeUpstreamServices("foosvc|0|1|not a decimal number"),
                    std::invalid_argument);
}
