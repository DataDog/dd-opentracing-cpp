#include "../src/propagation.h"
#include "mocks.h"

#include <string>

#define CATCH_CONFIG_MAIN
#include <datadog/catch2/catch.hpp>
using namespace datadog::opentracing;
namespace ot = opentracing;

// A Mock TextMapReader and TextMapWriter.
// Not in mocks.h since we only need it here for now.
struct MockTextMapCarrier : ot::TextMapReader, ot::TextMapWriter {
  MockTextMapCarrier() {}

  ot::expected<void> Set(ot::string_view key, ot::string_view value) const override {
    if (set_fails_after == 0) {
      return ot::make_unexpected(std::error_code(6, ot::propagation_error_category()));
    } else if (set_fails_after > 0) {
      set_fails_after--;
    }
    text_map[key] = value;
    return {};
  }

  ot::expected<ot::string_view> LookupKey(ot::string_view key) const override {
    return ot::make_unexpected(ot::lookup_key_not_supported_error);
  }

  ot::expected<void> ForeachKey(
      std::function<ot::expected<void>(ot::string_view key, ot::string_view value)> f)
      const override {
    for (const auto& key_value : text_map) {
      auto result = f(key_value.first, key_value.second);
      if (!result) return result;
    }
    return {};
  }

  mutable std::unordered_map<std::string, std::string> text_map;
  // Count-down to method failing. Negative means no failures.
  mutable int set_fails_after = -1;
};

TEST_CASE("SpanContext") {
  MockTextMapCarrier carrier{};
  SpanContext context{420, 123, {}};

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(carrier);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->trace_id() == 123);

      SECTION("even with extra keys") {
        carrier.Set("some junk thingy", "ayy lmao");
        auto sc = SpanContext::deserialize(carrier);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->id() == 420);
        REQUIRE(received_context->trace_id() == 123);
      }
    }
  }

  SECTION("serialise fails") {
    SECTION("when setting trace id fails") {
      carrier.set_fails_after = 0;
      auto err = context.serialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }

    SECTION("when setting parent id fails") {
      carrier.set_fails_after = 1;
      auto err = context.serialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }
  }

  SECTION("deserialise fails") {
    SECTION("when there are missing keys") {
      carrier.Set("x-datadog-trace-id", "123");
      carrier.Set("but where is parent-id??", "420");
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when there are formatted keys") {
      carrier.Set("x-datadog-trace-id", "The madman! This isn't even a number!");
      carrier.Set("x-datadog-parent-id", "420");
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }
  }
}
