#include "../src/propagation.h"
#include "mocks.h"

#include <string>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;
namespace ot = opentracing;

using dict = std::unordered_map<std::string, std::string>;

dict getBaggage(SpanContext* ctx) {
  dict baggage;
  ctx->ForeachBaggageItem([&baggage](const std::string& key, const std::string& value) -> bool {
    baggage[key] = value;
    return true;
  });
  return baggage;
}

TEST_CASE("SpanContext") {
  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  SpanContext context{420,
                      123,
                      std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep),
                      {{"ayy", "lmao"}, {"hi", "haha"}},
                      buffer};

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(carrier, buffer);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      REQUIRE(received_context->getSamplingPriority() != nullptr);
      REQUIRE(*received_context->getSamplingPriority() == SamplingPriority::SamplerKeep);
      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});

      SECTION("even with extra keys") {
        carrier.Set("some junk thingy", "ayy lmao");
        auto sc = SpanContext::deserialize(carrier, buffer);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->id() == 420);
        REQUIRE(received_context->traceId() == 123);
        REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});
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
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when there are formatted keys") {
      carrier.Set("x-datadog-trace-id", "The madman! This isn't even a number!");
      carrier.Set("x-datadog-parent-id", "420");
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when the sampling priority is whack") {
      carrier.Set("x-datadog-sampling-priority", "420");
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }
  }
}

TEST_CASE("Binary Span Context") {
  std::stringstream carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  SpanContext context{420,
                      123,
                      std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep),
                      {{"ayy", "lmao"}, {"hi", "haha"}},
                      buffer};

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(carrier, buffer);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      REQUIRE(received_context->getSamplingPriority() != nullptr);
      REQUIRE(*received_context->getSamplingPriority() == SamplingPriority::SamplerKeep);
      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});
    }
  }

  SECTION("serialise fails") {
    SECTION("when the writer is not 'good'") {
      carrier.clear(carrier.badbit);
      auto err = context.serialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::io_error));
      carrier.clear(carrier.goodbit);
    }
  }

  SECTION("deserialize fails") {
    SECTION("when traceId is missing") {
      carrier << "{ \"parent_id\": \"420\" }";
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when parent_id is missing") {
      carrier << "{ \"traceId\": \"123\" }";
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when the sampling priority is whack") {
      carrier << "{ \"traceId\": \"123\", \"parent_id\": \"420\", \"sampling_priority\": 42 }";
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when given invalid json data") {
      carrier << "something that isn't JSON";
      auto err = SpanContext::deserialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::invalid_argument));
    }
  }
}
