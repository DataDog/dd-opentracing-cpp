#include "../src/propagation.h"
#include <opentracing/tracer.h>
#include <string>
#include "../src/tracer.h"
#include "mocks.h"

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
  buffer->traces()[123].sampling_priority =
      std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
  SpanContext context{420, 123, {{"ayy", "lmao"}, {"hi", "haha"}}};

  auto propagation_styles =
      GENERATE(std::set<PropagationStyle>{PropagationStyle::Datadog},
               std::set<PropagationStyle>{PropagationStyle::B3},
               std::set<PropagationStyle>{PropagationStyle::Datadog, PropagationStyle::B3});

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier, buffer, propagation_styles));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(carrier, propagation_styles);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      auto status = received_context->getPropagationStatus();
      REQUIRE(status.first == true);
      REQUIRE(*status.second == SamplingPriority::SamplerKeep);
      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});

      SECTION("even with extra keys") {
        carrier.Set("some junk thingy", "ayy lmao");
        auto sc = SpanContext::deserialize(carrier, propagation_styles);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->id() == 420);
        REQUIRE(received_context->traceId() == 123);
        REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});

        SECTION("equality works") {
          // Can't compare to original SpanContext 'context', because has_propagated_ is unset and
          // deserialization sets it.
          MockTextMapCarrier carrier2{};
          REQUIRE(received_context->serialize(carrier2, buffer, propagation_styles));
          carrier2.Set("more junk", "ayy lmao");
          auto sc2 = SpanContext::deserialize(carrier2, propagation_styles);
          auto received_context2 = dynamic_cast<SpanContext*>(sc2->get());
          REQUIRE(*received_context2 == *received_context);
        }
      }
    }
  }

  SECTION("serialise fails") {
    SECTION("when setting trace id fails") {
      carrier.set_fails_after = 0;
      auto err = context.serialize(carrier, buffer, propagation_styles);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }

    SECTION("when setting parent id fails") {
      carrier.set_fails_after = 1;
      auto err = context.serialize(carrier, buffer, propagation_styles);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }
  }
}

TEST_CASE("deserialise fails") {
  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces()[123].sampling_priority =
      std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
  SpanContext context{420, 123, {{"ayy", "lmao"}, {"hi", "haha"}}};

  struct PropagationStyleTestCase {
    std::set<PropagationStyle> styles;
    std::string x_datadog_trace_id;
    std::string x_datadog_parent_id;
    std::string x_datadog_sampling_priority;
  };

  auto test_case = GENERATE(values<PropagationStyleTestCase>(
      {{{PropagationStyle::Datadog},
        "x-datadog-trace-id",
        "x-datadog-parent-id",
        "x-datadog-sampling-priority"},
       {{PropagationStyle::B3}, "X-B3-TraceId", "X-B3-SpanId", "X-B3-Sampled"}}));

  SECTION("when there are missing keys") {
    carrier.Set(test_case.x_datadog_trace_id, "123");
    carrier.Set("but where is parent-id??", "420");
    auto err = SpanContext::deserialize(carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }

  SECTION("when there are formatted keys") {
    carrier.Set(test_case.x_datadog_trace_id, "The madman! This isn't even a number!");
    carrier.Set(test_case.x_datadog_parent_id, "420");
    auto err = SpanContext::deserialize(carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }

  SECTION("when the sampling priority is whack") {
    carrier.Set(test_case.x_datadog_trace_id, "123");
    carrier.Set(test_case.x_datadog_parent_id, "456");
    carrier.Set(test_case.x_datadog_sampling_priority, "420");
    auto err = SpanContext::deserialize(carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }
}

TEST_CASE("SamplingPriority values are clamped apropriately for b3") {
  // first = value before serialization + clamping, second = value after.
  auto priority = GENERATE(values<std::pair<SamplingPriority, SamplingPriority>>(
      {{SamplingPriority::UserDrop, SamplingPriority::SamplerDrop},
       {SamplingPriority::SamplerDrop, SamplingPriority::SamplerDrop},
       {SamplingPriority::SamplerKeep, SamplingPriority::SamplerKeep},
       {SamplingPriority::UserKeep, SamplingPriority::SamplerKeep}}));

  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces()[123].sampling_priority = std::make_unique<SamplingPriority>(priority.first);
  SpanContext context{420, 123, {}};

  REQUIRE(context.serialize(carrier, buffer, {PropagationStyle::B3}));

  auto sc = SpanContext::deserialize(carrier, {PropagationStyle::B3});
  auto received_context = dynamic_cast<SpanContext*>(sc->get());
  REQUIRE(received_context);
  REQUIRE(received_context->id() == 420);
  REQUIRE(received_context->traceId() == 123);
  auto status = received_context->getPropagationStatus();
  REQUIRE(status.first == true);
  REQUIRE(*status.second == priority.second);
}

TEST_CASE("deserialize fails when there are conflicting b3 and datadog headers") {
  MockTextMapCarrier carrier{};
  carrier.Set("x-datadog-trace-id", "420");
  carrier.Set("x-datadog-parent-id", "421");
  carrier.Set("x-datadog-sampling-priority", "1");
  carrier.Set("X-B3-TraceId", "1A4");
  carrier.Set("X-B3-SpanId", "1A5");
  carrier.Set("X-B3-Sampled", "1");

  auto test_case = GENERATE(
      values<std::pair<std::string, std::string>>({{"x-datadog-trace-id", "666"},
                                                   {"x-datadog-parent-id", "666"},
                                                   {"x-datadog-sampling-priority", "2"},
                                                   {"X-B3-TraceId", "29A"},
                                                   {"X-B3-SpanId", "29A"},
                                                   {"X-B3-Sampled", "0"},
                                                   // Invalid values for B3 but not for Datadog.
                                                   {"X-B3-Sampled", "-1"},
                                                   {"X-B3-Sampled", "2"}}));
  carrier.Set(test_case.first, test_case.second);

  auto err = SpanContext::deserialize(carrier, {PropagationStyle::Datadog, PropagationStyle::B3});
  REQUIRE(!err);
  REQUIRE(err.error() == ot::span_context_corrupted_error);
}

TEST_CASE("Binary Span Context") {
  std::stringstream carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  SpanContext context{420, 123, {{"ayy", "lmao"}, {"hi", "haha"}}};
  buffer->traces()[123].sampling_priority =
      std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier, buffer));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(carrier);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      auto status = received_context->getPropagationStatus();
      REQUIRE(status.first == true);
      REQUIRE(*status.second == SamplingPriority::SamplerKeep);
      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});
    }
  }

  SECTION("serialise fails") {
    SECTION("when the writer is not 'good'") {
      carrier.clear(carrier.badbit);
      auto err = context.serialize(carrier, buffer);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::io_error));
      carrier.clear(carrier.goodbit);
    }
  }

  SECTION("deserialize fails") {
    SECTION("when traceId is missing") {
      carrier << "{ \"parent_id\": \"420\" }";
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when parent_id is missing") {
      carrier << "{ \"trace_id\": \"123\" }";
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when the sampling priority is whack") {
      carrier << "{ \"trace_id\": \"123\", \"parent_id\": \"420\", \"sampling_priority\": 42 }";
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when given invalid json data") {
      carrier << "something that isn't JSON";
      auto err = SpanContext::deserialize(carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::invalid_argument));
    }
  }
}

TEST_CASE("sampling behaviour") {
  auto sampler = std::make_shared<MockSampler>();
  auto writer = std::make_shared<MockWriter>(sampler);
  auto buffer = std::make_shared<WritingSpanBuffer>(writer);
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, getRealTime, getId, sampler}};
  ot::Tracer::InitGlobal(tracer);

  // There's two ways we can set the sampling priority. Either directly using the method, or
  // through a tag. Test both.
  auto setSamplingPriority = GENERATE(
      values<
          std::function<OptionalSamplingPriority(Span*, std::unique_ptr<UserSamplingPriority>)>>({
          [](Span* span, std::unique_ptr<UserSamplingPriority> p) {
            return span->setSamplingPriority(std::move(p));
          },
          [](Span* span, std::unique_ptr<UserSamplingPriority> p) {
            if (p != nullptr) {
              span->SetTag("sampling.priority", static_cast<int>(*p));
            } else {
              span->SetTag("sampling.priority", "");
            }
            return span->getSamplingPriority();
          },
      }));

  SECTION("sampling priority can be set on a root span") {
    // Root: ##########x
    //          ^ Set here, should succeed, not overridden by automatic set
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto p = setSamplingPriority(
        static_cast<Span*>(span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep));
    REQUIRE(p);
    REQUIRE(*p == SamplingPriority::UserKeep);
    span->Finish();

    auto& result = writer->traces[0][0];
    REQUIRE(result->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::UserKeep));
  }

  SECTION("sampling priority is assigned on a root span if otherwise unset") {
    // Root: ##########x
    //                 ^ Set here automatically if priority sampling enabled, should succeed
    sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    span->Finish();

    auto& result = writer->traces[0][0];
    REQUIRE(result->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
  }

  SECTION("sampling priority can not be set on a finished root span") {
    // Root: ##########x
    //         not set-^  ^-Cannot be set here
    sampler->sampling_priority = nullptr;
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    span->Finish();
    auto p = setSamplingPriority(
        static_cast<Span*>(span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep));
    REQUIRE(!p);

    auto& result = writer->traces[0][0];
    REQUIRE(result->metrics.find("_sampling_priority_v1") == result->metrics.end());
  }

  SECTION("sampling priority is assigned to a propagated root span (and then cannot be set)") {
    // Root: ####P#####x
    //           ^  ^ Cannot set, since already propagated
    //           | Set here automatically, should succeed
    sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
    auto span = ot::Tracer::Global()->StartSpan("operation_name");

    MockTextMapCarrier carrier;
    auto err = tracer->Inject(span->context(), carrier);

    // setSamplingPriority should fail, since it's already set & locked, and should return the
    // assigned value.
    REQUIRE(*setSamplingPriority(static_cast<Span*>(span.get()), nullptr) ==
            SamplingPriority::SamplerKeep);
    // Double-checking!
    REQUIRE(carrier.text_map["x-datadog-sampling-priority"] == "1");

    span->Finish();

    auto& result = writer->traces[0][0];
    REQUIRE(result->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
  }

  SECTION("sampling priority for an entire trace can be set on a child span") {
    // Root:  ##########x
    // Child: .#######x..
    //             ^ Set here, should succeed
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});

    auto p = setSamplingPriority(
        static_cast<Span*>(child_span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep));
    REQUIRE(p);
    REQUIRE(*p == SamplingPriority::UserKeep);

    child_span->Finish();
    span->Finish();

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::UserKeep));
    REQUIRE(trace[1]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::UserKeep));
  }

  SECTION("sampling priority is assigned on a trace if otherwise unset") {
    // Root:  ##########x
    // Child: .#######x.^
    //                  | Set here automatically
    sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});
    child_span->Finish();
    span->Finish();

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
    REQUIRE(trace[1]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
  }

  SECTION("sampling priority can be set until the root trace finishes") {
    // Root:  ##############x
    // Child: .#######x..^...
    //                   | Set here, should succeed
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});
    child_span->Finish();

    auto p = setSamplingPriority(
        static_cast<Span*>(span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep));
    REQUIRE(p);
    REQUIRE(*p == SamplingPriority::UserKeep);

    span->Finish();

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::UserKeep));
    REQUIRE(trace[1]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::UserKeep));
  }

  SECTION("sampling priority can not be set on a finished trace") {
    // Root:  ##########x
    // Child: .#######x..
    //                    ^ Cannot be set here
    sampler->sampling_priority = nullptr;
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});
    child_span->Finish();
    span->Finish();
    REQUIRE(!setSamplingPriority(
        static_cast<Span*>(span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep)));
    REQUIRE(!setSamplingPriority(
        static_cast<Span*>(child_span.get()),
        std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep)));

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics.find("_sampling_priority_v1") == trace[0]->metrics.end());
    REQUIRE(trace[1]->metrics.find("_sampling_priority_v1") == trace[1]->metrics.end());
  }

  SECTION(
      "sampling priority is assigned to a trace (and then cannot be set) when a child "
      "propagates") {
    // Root:  ##########x
    // Child: .##P#####x..
    //           ^   ^ Cannot be set here, nor on Root.
    //           | Assigned automatically here
    sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});

    MockTextMapCarrier carrier;
    auto err = tracer->Inject(child_span->context(), carrier);

    // setSamplingPriority should fail, since it's already set & locked, and should return the
    // assigned value.
    REQUIRE(*setSamplingPriority(static_cast<Span*>(span.get()), nullptr) ==
            SamplingPriority::SamplerKeep);
    REQUIRE(*setSamplingPriority(static_cast<Span*>(child_span.get()), nullptr) ==
            SamplingPriority::SamplerKeep);
    // Double-checking!
    REQUIRE(carrier.text_map["x-datadog-sampling-priority"] == "1");

    child_span->Finish();
    span->Finish();

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
    REQUIRE(trace[1]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
  }
}
