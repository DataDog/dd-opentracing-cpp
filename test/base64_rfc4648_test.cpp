// This test covers the base64 codec defined in `base64_rfc4648.h`.

#include "../src/base64_rfc4648.h"

#include <catch2/catch.hpp>

using namespace datadog::opentracing;

TEST_CASE("unpadded base64 codec") {
  struct TestCase {
    std::string decoded;
    std::string encoded;
  };

  // RFC 4648 base64 encoding, but without any trailing padding.
  auto test_case = GENERATE(values<TestCase>({{"hello, world!", "aGVsbG8sIHdvcmxkIQ=="},
                                              {"h", "aA=="},
                                              {"he", "aGU="},
                                              {"hel", "aGVs"},
                                              {"hell", "aGVsbA=="},
                                              {"hello", "aGVsbG8="}}));

  std::string encoded;
  appendBase64(encoded, test_case.decoded);
  REQUIRE(encoded == test_case.encoded);
}
