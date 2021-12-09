// This test covers the base64 codec defined in `base64_rfc4648_unpadded.h`.

#include "../src/base64_rfc4648_unpadded.h"

#include <catch2/catch.hpp>

using namespace datadog::opentracing;

TEST_CASE("unpadded base64 codec") {
  struct TestCase {
    std::string decoded;
    std::string encoded;
  };

  // RFC 4648 base64 encoding, but without any trailing padding.
  auto test_case = GENERATE(values<TestCase>({{"hello, world!", "aGVsbG8sIHdvcmxkIQ"},
                                              {"h", "aA"},
                                              {"he", "aGU"},
                                              {"hel", "aGVs"},
                                              {"hell", "aGVsbA"},
                                              {"hello", "aGVsbG8"}}));

  REQUIRE(base64_rfc4648_unpadded::encode(test_case.decoded) == test_case.encoded);
  std::string actual;
  base64_rfc4648_unpadded::decode(actual, test_case.encoded);
  REQUIRE(actual == test_case.decoded);
}
