#include "../src/limiter.h"

#include <catch2/catch.hpp>

#include "mocks.h"
using namespace datadog::opentracing;

TEST_CASE("limiter") {
  // Starting calendar time 2007-03-12 00:00:00
  std::tm start{};
  start.tm_mday = 12;
  start.tm_mon = 2;
  start.tm_year = 107;
  TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                 std::chrono::steady_clock::time_point{}};
  TimeProvider get_time = [&time]() { return time; };  // Mock clock.

  SECTION("limits requests") {
    Limiter lim(get_time, 1, 1.0, 1);
    auto first = lim.allow();
    auto second = lim.allow();
    REQUIRE(first.allowed);
    REQUIRE(!second.allowed);
  }

  SECTION("refreshes over time") {
    Limiter lim(get_time, 1, 1.0, 1);
    auto first = lim.allow();
    auto second = lim.allow();
    advanceTime(time, std::chrono::seconds(1));
    auto third = lim.allow();
    REQUIRE(first.allowed);
    REQUIRE(!second.allowed);
    REQUIRE(third.allowed);
  }

  SECTION("handles long intervals correctly") {
    Limiter lim(get_time, 1, 1.0, 1);
    auto first = lim.allow();
    advanceTime(time, std::chrono::seconds(2));
    auto second = lim.allow();
    auto third = lim.allow();
    REQUIRE(first.allowed);
    REQUIRE(second.allowed);
    REQUIRE(!third.allowed);
  }

  SECTION("calculates effective rate") {
    // starts off at 1.0, and decreases if nothing happens
    Limiter lim(get_time, 1, 1.0, 1);
    auto first = lim.allow();
    REQUIRE(first.allowed);
    REQUIRE(first.effective_rate == 1.0);
    auto second = lim.allow();
    REQUIRE(!second.allowed);
    REQUIRE(second.effective_rate == 0.95);
    // if 10 seconds pass, then the effective rate gets reset, so it should be
    // 9 seconds of 1.0 and one second of 1.0
    advanceTime(time, std::chrono::seconds(10));
    auto third = lim.allow();
    REQUIRE(third.allowed);
    REQUIRE(third.effective_rate == 1.0);
  }

  SECTION("updates tokens at sub-second intervals") {
    Limiter lim(get_time, 5, 5.0, 1);  // replace tokens @ 5.0 per second (i.e. every 0.2 seconds)
    // consume all the tokens first
    for (auto i = 0; i < 5; i++) {
      auto result = lim.allow();
      REQUIRE(result.allowed);
    }
    auto all_consumed = lim.allow();
    REQUIRE(!all_consumed.allowed);

    advanceTime(time, std::chrono::milliseconds(200));
    auto first = lim.allow();
    auto second = lim.allow();
    REQUIRE(first.allowed);
    REQUIRE(!second.allowed);  // only one token after 0.2s

    // refills to maximum, and can consume 5 tokens
    advanceTime(time, std::chrono::seconds(1));
    for (auto i = 0; i < 5; i++) {
      auto result = lim.allow();
      REQUIRE(result.allowed);
    }
    all_consumed = lim.allow();
    REQUIRE(!all_consumed.allowed);
  }

  SECTION("updates tokens at multi-second intervals") {
    Limiter lim(get_time, 1, 0.25, 1);  // replace tokens @ 0.25 per second (i.e. every 4 seconds)

    // 0 seconds (0s)
    auto result = lim.allow();
    REQUIRE(result.allowed);

    for (int i = 0; i < 3; ++i) {
      // 1s, 2s, 3s... still haven't released a token
      advanceTime(time, std::chrono::seconds(1));
      result = lim.allow();
      REQUIRE(!result.allowed);
    }

    // 4s... one token was just released
    advanceTime(time, std::chrono::seconds(1));
    result = lim.allow();
    REQUIRE(result.allowed);

    // still 4s... and we used that token already
    result = lim.allow();
    REQUIRE(!result.allowed);
  }

  SECTION("dedicated constructor configures based on desired allowed-per-second") {
    const double per_second = 23.97;
    Limiter lim(get_time, per_second);
    for (int i = 0; i < 24; ++i) {
      auto result = lim.allow();
      REQUIRE(result.allowed);
    }

    auto result = lim.allow();
    REQUIRE(!result.allowed);

    advanceTime(time, std::chrono::milliseconds(int(1 / per_second * 1000) + 1));
    result = lim.allow();
    REQUIRE(result.allowed);
    result = lim.allow();
    REQUIRE(!result.allowed);
  }
}
