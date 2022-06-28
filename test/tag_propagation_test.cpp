// This test covers the tag propagation formatting functions declared in
// `tag_propagation.h`.

#include "../src/tag_propagation.h"

#include <algorithm>
#include <catch2/catch.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace datadog::opentracing;

namespace {

std::string serializeTags(const std::unordered_map<std::string, std::string>& tags) {
  // The format does not require that the tags be sorted by name, but since
  // the order of elements in a `std::unordered_map` are not stable across
  // standard library versions, I sort them here for the sake of testing.
  std::vector<std::pair<std::string, std::string>> sorted_tags{tags.begin(), tags.end()};
  std::sort(sorted_tags.begin(), sorted_tags.end());

  std::string serialized_result;
  for (const auto& entry : sorted_tags) {
    const std::string& key = entry.first;
    const std::string& value = entry.second;
    appendTag(serialized_result, key, value);
  }

  return serialized_result;
}

}  // namespace

TEST_CASE("tag propagation codec expected values") {
  struct TestCase {
    ot::string_view name;
    ot::string_view encoded;
    std::unordered_map<std::string, std::string> decoded;
  };

  auto test_case = GENERATE(values<TestCase>(
      {// clang-format off
      {"example from RFC",
       "_dd.p.dm=-4,_dd.p.hello=world",
       {
         {"_dd.p.dm", "-4"},
         {"_dd.p.hello", "world"}
       }},
      {"empty",
       "",
       {}},
      {"one tag",
       "foo=bar",
       {
         {"foo", "bar"}
       }}
    }));
  // clang-format on

  SECTION("encodes to expected value") {
    const auto encoded = serializeTags(test_case.decoded);
    REQUIRE(encoded == test_case.encoded);
  }

  SECTION("decodes to expected value") {
    const auto decoded = deserializeTags(test_case.encoded);
    REQUIRE(decoded == test_case.decoded);
  }
}

TEST_CASE("tag propagation decoding duplicate tags") {
  SECTION("chooses last value when different") {
    const std::unordered_map<std::string, std::string> expected{{"dupe", "bar"}};
    REQUIRE(deserializeTags("dupe=foo,dupe=bar") == expected);
  }

  SECTION("chooses last value when the same") {
    const std::unordered_map<std::string, std::string> expected{{"dupe", "same"}};
    REQUIRE(deserializeTags("dupe=same,dupe=same") == expected);
  }
}

TEST_CASE("key/value items must contain an equal sign") {
  REQUIRE_THROWS_AS(deserializeTags("valid=version,invalid_version"), std::invalid_argument);
  // The trailing comma means that there's a second key=value that is the empty
  // string, and thus missing an equal sign.
  REQUIRE_THROWS_AS(deserializeTags("valid=version,"), std::invalid_argument);
}
