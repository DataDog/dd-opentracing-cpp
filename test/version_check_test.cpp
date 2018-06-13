#include <datadog/opentracing.h>

#include "../src/version_check.h"

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("version_check") {
  SECTION("success cases") {
    REQUIRE(equal_or_higher_version("1.3.0", "1.3.0"));  // Exact match.
    REQUIRE(equal_or_higher_version(
        "1.4.2", "1.3.0"));  // The case that triggered a need for version checking.
    REQUIRE(equal_or_higher_version("1.3.0-alpha", "1.3.0-alpha"));  // Exact match with label.
    REQUIRE(equal_or_higher_version("1.3.1", "1.3.0"));              // Higher patch number.
    REQUIRE(equal_or_higher_version("1.4.0", "1.3.0"));              // Higher minor version.
    REQUIRE(
        equal_or_higher_version("1.10.0", "1.9.9"));  // Confirm the check is numeric, not lexical.
  }
  SECTION("failure cases") {
    REQUIRE(!equal_or_higher_version("2.0.0", "1.99.99"));  // Major version is too high.
    REQUIRE(!equal_or_higher_version("1.3.0", "1.3.1"));    // Version is lower.
    REQUIRE(
        !equal_or_higher_version("1.3.0-alpha", "1.3.0"));  // Version is lower because of label.
  }
}
