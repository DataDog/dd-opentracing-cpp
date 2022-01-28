// This test covers the "upstream service" formatting functions declared in
// `upstream_service.h`.

#include "../src/upstream_service.h"

#include <catch2/catch.hpp>
#include <cmath>
#include <ostream>
#include <stdexcept>
#include <string>

using namespace datadog::opentracing;

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
                                              {0.98761, "0.9877"},
                                              {0.98769, "0.9877"},
                                              {std::nan(""), ""}}));

  std::string result;
  appendSamplingRate(result, test_case.input);
  REQUIRE(result == test_case.output);
}

TEST_CASE("appendUpstreamService") {
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
      {{"mysvc", SamplingPriority::UserKeep, int(SamplingMechanism::Rule), 0.01234}},
      "bXlzdmM|2|3|0.0124"
    },
    { // two services
      {{"yoursvc", SamplingPriority::SamplerDrop, 1337, 1.0},
       {"mysvc", SamplingPriority::UserKeep, int(SamplingMechanism::Rule), 0.01234}},
      "eW91cnN2Yw|0|1337|1.0000;bXlzdmM|2|3|0.0124"
    },
    { // example based on internal design document
      {{"mcnulty-web", SamplingPriority::SamplerDrop, int(SamplingMechanism::AgentRate), std::nan("")},
       {"trace-stats-query", SamplingPriority::UserKeep, int(SamplingMechanism::Manual), std::nan("")}},
      "bWNudWx0eS13ZWI|0|1|;dHJhY2Utc3RhdHMtcXVlcnk|2|4|"
    }
  }));
  // clang-format on

  std::string encoded;
  for (const auto& upstream_service : test_case.decoded) {
    appendUpstreamService(encoded, upstream_service);
  }
  REQUIRE(encoded == test_case.encoded);
}
