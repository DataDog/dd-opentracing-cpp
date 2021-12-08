// This test covers the "upstream service" formatting functions declared in
// `upstream_service.h`.

#include "../src/upstream_service.h"

#include <catch2/catch.hpp>
#include <cmath>
#include <ostream>
#include <string>

using namespace datadog::opentracing;

namespace datadog {
namespace opentracing {

// catch2 will print values in failed assertions, but only if those values have a corresponding `operator<<`.
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

TEST_CASE("unpadded base64 encoding") {
  struct TestCase {
    std::string input;
    std::string output;
  };

  // RFC 4648 base64 encoding, but without any trailing padding.
  auto test_case = GENERATE(values<TestCase>({{"hello, world!", "aGVsbG8sIHdvcmxkIQ"},
                                              {"h", "aA"},
                                              {"he", "aGU"},
                                              {"hel", "aGVs"},
                                              {"hell", "aGVsbA"},
                                              {"hello", "aGVsbG8"}}));

  std::string result;
  appendAsBase64Unpadded(result, test_case.input);
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
      {{"mysvc", SamplingPriority::UserKeep, KnownSamplingMechanism::Rule, 0.01234, {"extra", "junk"}}},
      "bXlzdmM|2|3|0.0124|extra|junk"
    },
    { // two services
      {{"yoursvc", SamplingPriority::SamplerDrop, UnknownSamplingMechanism{1337}, 1.0, {}},
       {"mysvc", SamplingPriority::UserKeep, KnownSamplingMechanism::Rule, 0.01234, {"extra", "junk"}}},
      "eW91cnN2Yw|0|1337|1.0000;bXlzdmM|2|3|0.0124|extra|junk"
    },
    { // example based on internal design document
      {{"mcnulty-web", SamplingPriority::SamplerDrop, KnownSamplingMechanism::AgentRate, std::nan(""), {}},
       {"trace-stats-query", SamplingPriority::UserKeep, KnownSamplingMechanism::Manual, std::nan(""), {"foo"}}},
      "bWNudWx0eS13ZWI|0|1|;dHJhY2Utc3RhdHMtcXVlcnk|2|4||foo"
    }
  }));
  // clang-format on

  REQUIRE(serializeUpstreamServices(test_case.decoded) == test_case.encoded);
  REQUIRE(deserializeUpstreamServices(test_case.encoded) == test_case.decoded);
}
