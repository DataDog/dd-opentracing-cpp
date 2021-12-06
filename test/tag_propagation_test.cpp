// This test covers the tag propagation formatting functions declared in
// `tag_propagation.h`.

#include "../src/tag_propagation.h"

#include <catch2/catch.hpp>

#include <algorithm>
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

} // namespace

TEST_CASE("tag propagation codec expected values") {
    struct TestCase {
        ot::string_view name;
        ot::string_view encoded;
        std::unordered_map<std::string, std::string> decoded;
    };

    // clang-format off
    auto test_case = GENERATE(values<TestCase>({
        {"example from RFC",
         "_dd.p.hello=world,_dd.p.upstream_services=bWNudWx0eS13ZWI|0|1;dHJhY2Utc3RhdHMtcXVlcnk|2|4",
         {
             {"_dd.p.upstream_services", "bWNudWx0eS13ZWI|0|1;dHJhY2Utc3RhdHMtcXVlcnk|2|4"},
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
