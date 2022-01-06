#include "../src/propagation.h"

#include <datadog/tags.h>
#include <opentracing/ext/tags.h>
#include <opentracing/tracer.h>

#include <cassert>
#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "../src/span.h"
#include "../src/tag_propagation.h"
#include "../src/tracer.h"
#include "mocks.h"
using namespace datadog::opentracing;
namespace tags = datadog::tags;
namespace ot = opentracing;

dict getBaggage(SpanContext* ctx) {
  dict baggage;
  ctx->ForeachBaggageItem([&baggage](const std::string& key, const std::string& value) -> bool {
    baggage[key] = value;
    return true;
  });
  return baggage;
}

TEST_CASE("SpanContext") {
  auto logger = std::make_shared<const MockLogger>();
  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces().emplace(std::make_pair(
      123, PendingTrace{logger, 123,
                        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep)}));
  SpanContext context{logger, 420, 123, "synthetics", {{"ayy", "lmao"}, {"hi", "haha"}}};

  auto propagation_styles =
      GENERATE(std::set<PropagationStyle>{PropagationStyle::Datadog},
               std::set<PropagationStyle>{PropagationStyle::B3},
               std::set<PropagationStyle>{PropagationStyle::Datadog, PropagationStyle::B3});
  auto priority_sampling = GENERATE(false, true);

  SECTION("can be serialized") {
    REQUIRE(context.serialize(carrier, buffer, propagation_styles, priority_sampling));

    // NGINX tracing harness requires that headers injected into requests are on a whitelist.
    SECTION("headers match the header whitelist") {
      std::set<std::string> headers_got;
      for (auto header : carrier.text_map) {
        // It's fine to have headers not on the list, so these ot-baggage-xxx headers are safely
        // ignored. However we still want to test exact equality between whitelist and
        // headers-we-actually-need.
        if (header.first.find(baggage_prefix) == 0) {
          continue;
        }
        headers_got.insert(header.first);
      }  // This was still less LoC than using std::transformer. Somehow EVEN JAVA gets this right
         // these days...
      // clang-format off
      // Even in C++20, the loop is still better:
      //
      //     using std::views;
      //
      //     auto headers_view =
      //         carrier.text_map |
      //         filter([] (const auto& entry) { return entry.first.starts_with(baggage_prefix); }) |
      //         transform([] (const auto& entry) { return entry.second; });
      //
      //     headers_got.insert(headers_view.begin(), headers_view.end());
      //
      // Maybe in C++23 we'll have:
      //
      //     using std::views;
      //     using std::ranges;
      //
      //     carrier.text_map |
      //         filter([] (const auto& entry) { return entry.first.starts_with(baggage_prefix); }) |
      //         transform([] (const auto& entry) { return entry.second; }) |
      //         to(headers_got);
      //
      // Just keep writing loops.
      //
      // Maybe in C++38 we'll have:
      //
      //     headers_got = [entry.second for const auto& entry : carrier.text_map if entry.first.starts_with(baggage_prefix)];
      //
      // clang-format on
      //
      std::set<std::string> headers_want;
      for (auto header : getPropagationHeaderNames(propagation_styles, priority_sampling)) {
        headers_want.insert(header);
      }
      // With the addition of "x-datadog-tags", it's difficult to know which
      // headers will be injected.
      // For example, "x-datadog-tags" will not be injected in this test section,
      // because no "x-datadog-tags" is extracted and no sampling decision is made.
      // In actual usage, either a sampling decision will be made (either by an
      // extracted sampling priority or by the sampler) or "x-datadog-tags"
      // will be extracted, and so the header will always be injected.
      // To account for this, I require that `headers_got` is a subset of
      // `headers_want`, not that they are equivalent.
      REQUIRE(std::includes(headers_want.begin(), headers_want.end(), headers_got.begin(),
                            headers_got.end()));
    }

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(logger, carrier, propagation_styles);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      if (priority_sampling) {
        auto priority = received_context->getPropagatedSamplingPriority();
        REQUIRE(priority != nullptr);
        REQUIRE(*priority == SamplingPriority::SamplerKeep);
      }
      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});

      SECTION("even with extra keys") {
        carrier.Set("some junk thingy", "ayy lmao");
        auto sc = SpanContext::deserialize(logger, carrier, propagation_styles);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->id() == 420);
        REQUIRE(received_context->traceId() == 123);
        REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});

        SECTION("equality works") {
          // Can't compare to original SpanContext 'context', because has_propagated_ is unset and
          // deserialization sets it.
          MockTextMapCarrier carrier2{};
          REQUIRE(received_context->serialize(carrier2, buffer, propagation_styles,
                                              priority_sampling));
          carrier2.Set("more junk", "ayy lmao");
          auto sc2 = SpanContext::deserialize(logger, carrier2, propagation_styles);
          auto received_context2 = dynamic_cast<SpanContext*>(sc2->get());
          REQUIRE(*received_context2 == *received_context);
        }
      }

      SECTION("even with leading whitespace in integer fields") {
        carrier.Set("x-datadog-trace-id", "    123");
        auto sc = SpanContext::deserialize(logger, carrier, propagation_styles);
        REQUIRE(sc);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->traceId() == 123);
      }

      SECTION("even with trailing whitespace in integer fields") {
        carrier.Set("x-datadog-trace-id", "123    ");
        auto sc = SpanContext::deserialize(logger, carrier, propagation_styles);
        REQUIRE(sc);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->traceId() == 123);
      }

      SECTION("even with whitespace surrounding integer fields") {
        carrier.Set("x-datadog-trace-id", "  123    ");
        auto sc = SpanContext::deserialize(logger, carrier, propagation_styles);
        REQUIRE(sc);
        auto received_context = dynamic_cast<SpanContext*>(sc->get());
        REQUIRE(received_context);
        REQUIRE(received_context->traceId() == 123);
      }
    }
    SECTION("can access ids") {
      REQUIRE(context.ToTraceID() == "123");
      REQUIRE(context.ToSpanID() == "420");
    }
    SECTION("can be cloned") {
      auto cloned_context = context.Clone();
      REQUIRE(cloned_context != nullptr);
      auto cloned = dynamic_cast<SpanContext&>(*cloned_context);
      REQUIRE(context.id() == cloned.id());
      REQUIRE(context.traceId() == cloned.traceId());
      REQUIRE(context.origin() == cloned.origin());
      REQUIRE(getBaggage(&context) == getBaggage(&cloned));
      REQUIRE(context.getPropagatedSamplingPriority() == cloned.getPropagatedSamplingPriority());
      // Modifications don't affect the original.
      cloned.setBaggageItem("this", "that");
      REQUIRE(getBaggage(&context) != getBaggage(&cloned));
    }
  }

  SECTION("serialize fails") {
    SECTION("when setting trace id fails") {
      carrier.set_fails_after = 0;
      auto err = context.serialize(carrier, buffer, propagation_styles, priority_sampling);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }

    SECTION("when setting parent id fails") {
      carrier.set_fails_after = 1;
      auto err = context.serialize(carrier, buffer, propagation_styles, priority_sampling);
      REQUIRE(!err);
      REQUIRE(err.error() == std::error_code(6, ot::propagation_error_category()));
    }
  }
}

TEST_CASE("deserialize fails") {
  auto logger = std::make_shared<const MockLogger>();
  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces().emplace(std::make_pair(
      123, PendingTrace{logger, 123,
                        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep)}));
  SpanContext context{logger, 420, 123, "", {{"ayy", "lmao"}, {"hi", "haha"}}};

  struct PropagationStyleTestCase {
    std::set<PropagationStyle> styles;
    std::string x_datadog_trace_id;
    std::string x_datadog_parent_id;
    std::string x_datadog_sampling_priority;
    std::string x_datadog_origin;
  };

  auto test_case = GENERATE(values<PropagationStyleTestCase>({{{PropagationStyle::Datadog},
                                                               "x-datadog-trace-id",
                                                               "x-datadog-parent-id",
                                                               "x-datadog-sampling-priority",
                                                               "x-datadog-origin"},
                                                              {{PropagationStyle::B3},
                                                               "X-B3-TraceId",
                                                               "X-B3-SpanId",
                                                               "X-B3-Sampled",
                                                               "x-datadog-origin"}}));

  SECTION("when there are missing keys") {
    carrier.Set(test_case.x_datadog_trace_id, "123");
    carrier.Set("but where is parent-id??", "420");
    auto err = SpanContext::deserialize(logger, carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }

  SECTION("but not if origin is nonempty") {
    carrier.Set(test_case.x_datadog_origin, "The Shire");
    carrier.Set(test_case.x_datadog_trace_id, "123");
    carrier.Set(test_case.x_datadog_sampling_priority, "1");
    // Parent ID is missing, but it's ok because Origin is nonempty.
    auto context = SpanContext::deserialize(logger, carrier, test_case.styles);
    REQUIRE(context);   // not an error
    REQUIRE(*context);  // not a null context
  }

  SECTION("when there are formatted keys") {
    carrier.Set(test_case.x_datadog_trace_id, "The madman! This isn't even a number!");
    carrier.Set(test_case.x_datadog_parent_id, "420");
    auto err = SpanContext::deserialize(logger, carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }

  SECTION("when the sampling priority is whack") {
    carrier.Set(test_case.x_datadog_trace_id, "123");
    carrier.Set(test_case.x_datadog_parent_id, "456");
    carrier.Set(test_case.x_datadog_sampling_priority, "420");
    auto err = SpanContext::deserialize(logger, carrier, test_case.styles);
    REQUIRE(!err);
    REQUIRE(err.error() == ot::span_context_corrupted_error);
  }

  SECTION("when decimal integer IDs start decimal but have hex characters") {
    carrier.Set(test_case.x_datadog_trace_id, "123deadbeef");
    auto err = SpanContext::deserialize(logger, carrier, test_case.styles);
    REQUIRE(!err);
  }
}

TEST_CASE("SamplingPriority values are clamped apropriately for b3") {
  // first = value before serialization + clamping, second = value after.
  auto priority = GENERATE(values<std::pair<SamplingPriority, SamplingPriority>>(
      {{SamplingPriority::UserDrop, SamplingPriority::SamplerDrop},
       {SamplingPriority::SamplerDrop, SamplingPriority::SamplerDrop},
       {SamplingPriority::SamplerKeep, SamplingPriority::SamplerKeep},
       {SamplingPriority::UserKeep, SamplingPriority::SamplerKeep}}));

  auto logger = std::make_shared<const MockLogger>();
  MockTextMapCarrier carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces().emplace(std::make_pair(
      123, PendingTrace{logger, 123, std::make_unique<SamplingPriority>(priority.first)}));
  SpanContext context{logger, 420, 123, "", {}};

  REQUIRE(context.serialize(carrier, buffer, {PropagationStyle::B3}, true));

  auto sc = SpanContext::deserialize(logger, carrier, {PropagationStyle::B3});
  auto received_context = dynamic_cast<SpanContext*>(sc->get());
  REQUIRE(received_context);
  REQUIRE(received_context->id() == 420);
  REQUIRE(received_context->traceId() == 123);
  auto received_priority = received_context->getPropagatedSamplingPriority();
  REQUIRE(received_priority != nullptr);
  REQUIRE(*received_priority == priority.second);
}

TEST_CASE("deserialize fails when there are conflicting b3 and datadog headers") {
  auto logger = std::make_shared<const MockLogger>();
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

  auto err =
      SpanContext::deserialize(logger, carrier, {PropagationStyle::Datadog, PropagationStyle::B3});
  REQUIRE(!err);
  REQUIRE(err.error() == ot::span_context_corrupted_error);
}

TEST_CASE("deserialize returns a null context if both trace ID and parent ID are missing") {
  auto logger = std::make_shared<const MockLogger>();

  SECTION("from JSON") {
    std::istringstream json("{}");
    const auto result = SpanContext::deserialize(logger, json);
    REQUIRE(result.has_value());
    REQUIRE(!result.value());
  }

  SECTION("from a text map") {
    MockTextMapCarrier carrier;
    const auto result = SpanContext::deserialize(logger, carrier, {PropagationStyle::Datadog});
    REQUIRE(result.has_value());
    REQUIRE(!result.value());
  }
}

TEST_CASE("Binary Span Context") {
  auto logger = std::make_shared<const MockLogger>();
  std::stringstream carrier{};
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces().emplace(std::make_pair(
      123, PendingTrace{logger, 123,
                        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep)}));
  auto priority_sampling = GENERATE(false, true);

  SECTION("can be serialized") {
    SpanContext context{logger, 420, 123, "", {{"ayy", "lmao"}, {"hi", "haha"}}};
    REQUIRE(context.serialize(carrier, buffer, priority_sampling));

    SECTION("can be deserialized") {
      auto sc = SpanContext::deserialize(logger, carrier);
      auto received_context = dynamic_cast<SpanContext*>(sc->get());
      REQUIRE(received_context);
      REQUIRE(received_context->id() == 420);
      REQUIRE(received_context->traceId() == 123);
      if (priority_sampling) {
        auto priority = received_context->getPropagatedSamplingPriority();
        REQUIRE(priority != nullptr);
        REQUIRE(*priority == SamplingPriority::SamplerKeep);
      }

      REQUIRE(getBaggage(received_context) == dict{{"ayy", "lmao"}, {"hi", "haha"}});
    }
  }

  SECTION("serialize fails") {
    SpanContext context{logger, 420, 123, "", {{"ayy", "lmao"}, {"hi", "haha"}}};
    SECTION("when the writer is not 'good'") {
      carrier.clear(carrier.badbit);
      auto err = context.serialize(carrier, buffer, priority_sampling);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::io_error));
      carrier.clear(carrier.goodbit);
    }
  }

  SECTION("deserialize fails") {
    SECTION("when traceId is missing") {
      carrier << "{ \"parent_id\": \"420\" }";
      auto err = SpanContext::deserialize(logger, carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when parent_id is missing") {
      carrier << "{ \"trace_id\": \"123\" }";
      auto err = SpanContext::deserialize(logger, carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when the sampling priority is whack") {
      carrier << "{ \"trace_id\": \"123\", \"parent_id\": \"420\", \"sampling_priority\": 42 }";
      auto err = SpanContext::deserialize(logger, carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == ot::span_context_corrupted_error);
    }

    SECTION("when given invalid json data") {
      carrier << "something that isn't JSON";
      auto err = SpanContext::deserialize(logger, carrier);
      REQUIRE(!err);
      REQUIRE(err.error() == std::make_error_code(std::errc::invalid_argument));
    }
  }
}

TEST_CASE("sampling behaviour") {
  auto logger = std::make_shared<MockLogger>();
  auto sampler = std::make_shared<MockRulesSampler>();
  auto writer = std::make_shared<MockWriter>(sampler);
  auto buffer =
      std::make_shared<WritingSpanBuffer>(logger, writer, sampler, WritingSpanBufferOptions{});
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, getRealTime, getId}};
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
    REQUIRE(span);

    MockTextMapCarrier carrier;
    auto err = tracer->Inject(span->context(), carrier);

    // setSamplingPriority should fail, since it's already set & locked, and should return the
    // assigned value.
    auto priority = setSamplingPriority(static_cast<Span*>(span.get()), nullptr);
    REQUIRE(priority);
    REQUIRE(*priority == SamplingPriority::SamplerKeep);
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
    // Child doesn't have metric set.
    REQUIRE(trace[0]->metrics.find("_sampling_priority_v1") == trace[0]->metrics.end());
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
    REQUIRE(trace[0]->metrics.find("_sampling_priority_v1") == trace[0]->metrics.end());
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
    REQUIRE(trace[0]->metrics.find("_sampling_priority_v1") == trace[0]->metrics.end());
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
    REQUIRE(span);
    auto child_span = tracer->StartSpan("childA", {ot::ChildOf(&span->context())});
    REQUIRE(child_span);

    MockTextMapCarrier carrier;
    auto err = tracer->Inject(child_span->context(), carrier);

    // setSamplingPriority should fail, since it's already set & locked, and should return the
    // assigned value.
    auto priority = setSamplingPriority(static_cast<Span*>(span.get()), nullptr);
    REQUIRE(priority);
    REQUIRE(*priority == SamplingPriority::SamplerKeep);
    priority = setSamplingPriority(static_cast<Span*>(child_span.get()), nullptr);
    REQUIRE(*priority == SamplingPriority::SamplerKeep);
    // Double-checking!
    REQUIRE(carrier.text_map["x-datadog-sampling-priority"] == "1");

    child_span->Finish();
    span->Finish();

    auto& trace = writer->traces[0];
    REQUIRE(trace[0]->metrics.find("_sampling_priority_v1") == trace[0]->metrics.end());
    REQUIRE(trace[1]->metrics["_sampling_priority_v1"] ==
            static_cast<int>(SamplingPriority::SamplerKeep));
  }
}

TEST_CASE("force tracing behaviour") {
  auto logger = std::make_shared<MockLogger>();
  auto sampler = std::make_shared<MockRulesSampler>();
  auto writer = std::make_shared<MockWriter>(sampler);
  auto buffer =
      std::make_shared<WritingSpanBuffer>(logger, writer, sampler, WritingSpanBufferOptions{});
  TracerOptions tracer_options{"", 0, "service_name", "web"};
  std::shared_ptr<Tracer> tracer{new Tracer{tracer_options, buffer, getRealTime, getId}};
  ot::Tracer::InitGlobal(tracer);

  auto priority = GENERATE(values<std::pair<std::string, SamplingPriority>>(
      {{tags::manual_keep, SamplingPriority::UserKeep},
       {tags::manual_drop, SamplingPriority::UserDrop}}));

  SECTION("set sampling priority via manual.* tags") {
    auto span = ot::Tracer::Global()->StartSpan("operation_name");
    span->SetTag(priority.first, {});
    auto p = static_cast<Span*>(span.get())->getSamplingPriority();
    REQUIRE(p);
    REQUIRE(*p == priority.second);
  }
}

TEST_CASE("origin header propagation") {
  auto logger = std::make_shared<const MockLogger>();
  auto sampler = std::make_shared<MockRulesSampler>();
  auto buffer = std::make_shared<MockBuffer>();
  buffer->traces().emplace(std::make_pair(
      123, PendingTrace{logger, 123,
                        std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep)}));

  std::shared_ptr<Tracer> tracer{new Tracer{{}, buffer, getRealTime, getId}};
  SpanContext context{logger, 420, 123, "madeuporigin", {{"ayy", "lmao"}, {"hi", "haha"}}};
  ot::Tracer::InitGlobal(tracer);

  SECTION("the origin header is injected") {
    MockTextMapCarrier carrier;
    auto ok = tracer->Inject(context, carrier);

    REQUIRE(ok);
    REQUIRE(carrier.text_map["x-datadog-origin"] == "madeuporigin");
  }

  SECTION("the propagated origin header can be extracted") {
    std::stringstream carrier;
    auto ok = tracer->Inject(context, carrier);
    REQUIRE(ok);

    auto span_context_maybe = tracer->Extract(carrier);
    REQUIRE(span_context_maybe);

    // A child span inherits the origin from the parent.
    auto span = tracer->StartSpan("child", {ChildOf(span_context_maybe->get())});

    MockTextMapCarrier tmc;
    ok = tracer->Inject(span->context(), tmc);
    REQUIRE(ok);
    REQUIRE(tmc.text_map["x-datadog-origin"] == "madeuporigin");

    span->Finish();
  }

  SECTION("the local root span is tagged with _dd.origin") {
    std::stringstream carrier;
    auto ok = tracer->Inject(context, carrier);
    REQUIRE(ok);

    auto span_context_maybe = tracer->Extract(carrier);
    REQUIRE(span_context_maybe);

    // A child span inherits the origin from the parent.
    auto spanA = tracer->StartSpan("toplevel", {ChildOf(span_context_maybe->get())});
    auto spanB = tracer->StartSpan("midlevel", {ChildOf(&spanA->context())});
    auto spanC = tracer->StartSpan("bottomlevel", {ChildOf(&spanB->context())});
    spanC->Finish();
    spanB->Finish();
    spanA->Finish();

    auto& traces = buffer->traces();
    auto it = traces.find(123);
    REQUIRE(it != traces.end());
    auto& spans = it->second.finished_spans;
    REQUIRE(spans->size() == 3);
    // The local root span should have the tag.
    auto& meta = spans->at(2)->meta;
    REQUIRE(meta["_dd.origin"] == "madeuporigin");
    // The other spans should also have the tag.
    meta = spans->at(0)->meta;
    REQUIRE(meta.find("_dd.origin") != meta.end());
    meta = spans->at(1)->meta;
    REQUIRE(meta.find("_dd.origin") != meta.end());
  }

  SECTION("only trace id and origin headers are required") {
    MockTextMapCarrier tmc;
    tmc.text_map["x-datadog-trace-id"] = "321";
    tmc.text_map["x-datadog-origin"] = "madeuporigin";

    auto span_context_maybe = tracer->Extract(tmc);
    REQUIRE(span_context_maybe);

    auto sc = dynamic_cast<SpanContext*>(span_context_maybe->get());
    REQUIRE(sc->traceId() == 321);
    REQUIRE(sc->origin() == "madeuporigin");
  }
}

TEST_CASE("propagated Datadog tags (x-datadog-tags)") {
  // `x-datadog-tags` is a header containing special "trace tags" that
  // should be propagated with outgoing requests, possibly with added
  // information about a sampling decision made by us (this service).

  TracerOptions options;
  options.service = "zappasvc";
  options.environment = "staging";
  options.trace_tags_propagation_max_length = 512;

  auto logger = std::make_shared<const MockLogger>();
  auto sampler = std::make_shared<MockRulesSampler>();
  auto buffer = std::make_shared<MockBuffer>(sampler, options.service,
                                             options.trace_tags_propagation_max_length);

  auto tracer = std::make_shared<Tracer>(options, buffer, getRealTime, getId, logger);

  SECTION("is injected") {
    SECTION("as it was extracted, if our sampling decision does not differ from the previous") {
      const std::string serialized_tags =
          "_dd.p.hello=world,_dd.p.upstream_services=dHJhY2Utc3RhdHMtcXVlcnk|2|4|";
      // Our sampler will make the same decision as dHJhY2Utc3RhdHMtcXVlcnk.
      sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserKeep);

      nlohmann::json json_to_extract;
      json_to_extract["tags"] = serialized_tags;
      json_to_extract["trace_id"] = "123";
      json_to_extract["parent_id"] = "456";
      std::istringstream to_extract(json_to_extract.dump());

      auto maybe_context = tracer->Extract(to_extract);
      REQUIRE(maybe_context);
      auto& context = maybe_context.value();
      REQUIRE(context);

      auto span = tracer->StartSpan("OperationMoonUnit", {ot::ChildOf(context.get())});
      REQUIRE(span);

      std::ostringstream injected;
      auto result = tracer->Inject(span->context(), injected);
      REQUIRE(result);

      auto injected_json = nlohmann::json::parse(injected.str());
      REQUIRE(deserializeTags(injected_json["tags"].get<std::string>()) ==
              deserializeTags(serialized_tags));
    }

    SECTION(
        "including an UpstreamService for us, if our sampling decision differs from the "
        "previous") {
      // `serialized_tags` is based off of an example in the internal RFC (the
      // choice of this value here is arbitrary).
      const std::string serialized_tags =
          "_dd.p.hello=world,_dd.p.upstream_services=bWNudWx0eS13ZWI|0|1|;"
          "dHJhY2Utc3RhdHMtcXVlcnk|2|4|";
      // Our sampler will make a decision different from dHJhY2Utc3RhdHMtcXVlcnk's.
      sampler->sampling_priority =
          std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
      sampler->sampling_mechanism = KnownSamplingMechanism::Default;
      sampler->applied_rate = sampler->priority_rate = 1.0;

      nlohmann::json json_to_extract;
      json_to_extract["tags"] = serialized_tags;
      json_to_extract["trace_id"] = "123";
      json_to_extract["parent_id"] = "456";
      std::istringstream to_extract(json_to_extract.dump());

      auto maybe_context = tracer->Extract(to_extract);
      REQUIRE(maybe_context);
      auto& context = maybe_context.value();
      REQUIRE(context);

      auto span = tracer->StartSpan("OperationMoonUnit", {ot::ChildOf(context.get())});
      REQUIRE(span);

      std::ostringstream injected;
      auto result = tracer->Inject(span->context(), injected);
      REQUIRE(result);

      auto injected_json = nlohmann::json::parse(injected.str());

      // The injected tags will look like `serialized_tags`, but with an
      // additional `UpstreamService` appended describing our sampling
      // decision (because it differs from the previous in
      // `serialized_tags`).
      UpstreamService expected_annex;
      expected_annex.service_name = options.service;
      assert(sampler->sampling_priority);
      expected_annex.sampling_priority = *sampler->sampling_priority;
      // Default → priority sampling without an agent-provided rate.
      expected_annex.sampling_mechanism = KnownSamplingMechanism::Default;
      expected_annex.sampling_rate = sampler->priority_rate;

      auto expected_tags = deserializeTags(serialized_tags);
      auto serialized_services = expected_tags.find("_dd.p.upstream_services");
      REQUIRE(serialized_services != expected_tags.end());
      auto services = deserializeUpstreamServices(serialized_services->second);
      services.push_back(expected_annex);
      serialized_services->second = serializeUpstreamServices(services);

      REQUIRE(deserializeTags(injected_json["tags"].get<std::string>()) == expected_tags);
    }

    SECTION(
        "including an UpstreamService for us, if we are the first to make a sampling decision") {
      // Let's omit "tags" ("x-datadog-tags") entirely from the extracted context.

      sampler->sampling_priority =
          std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
      sampler->sampling_mechanism = KnownSamplingMechanism::Default;
      sampler->applied_rate = sampler->priority_rate = 1.0;

      nlohmann::json json_to_extract;
      json_to_extract["trace_id"] = "123";
      json_to_extract["parent_id"] = "456";
      std::istringstream to_extract(json_to_extract.dump());

      auto maybe_context = tracer->Extract(to_extract);
      REQUIRE(maybe_context);
      auto& context = maybe_context.value();
      REQUIRE(context);

      auto span = tracer->StartSpan("OperationMoonUnit", {ot::ChildOf(context.get())});
      REQUIRE(span);

      std::ostringstream injected;
      auto result = tracer->Inject(span->context(), injected);
      REQUIRE(result);

      auto injected_json = nlohmann::json::parse(injected.str());

      // The injected tags will contain only the "_dd.p.upstream_services"
      // tag, which will contain a single `UpstreamService` record describing
      // our sampling decision.
      UpstreamService expected_annex;
      expected_annex.service_name = options.service;
      assert(sampler->sampling_priority);
      expected_annex.sampling_priority = *sampler->sampling_priority;
      // Default → priority sampling without an agent-provided rate.
      expected_annex.sampling_mechanism = KnownSamplingMechanism::Default;
      expected_annex.sampling_rate = sampler->priority_rate;

      dict expected_tags;
      expected_tags.emplace("_dd.p.upstream_services",
                            serializeUpstreamServices({expected_annex}));

      REQUIRE(deserializeTags(injected_json["tags"].get<std::string>()) == expected_tags);
    }
  }

  SECTION("is not injected") {
    SECTION("when the resulting header value is longer than the configured maximum") {
      // Create a list of tags that, serialized, are longer than the limit.
      // The tracer will extract the list, but then when it goes to inject it, it will fail.
      const std::string serialized_tags = "_dd.p.hello=" + std::string(1024, 'x');
      REQUIRE(serialized_tags.size() > 512);  // by construction

      // Don't make a sampling decision (though it doesn't matter for this
      // section).
      sampler->sampling_priority = nullptr;

      nlohmann::json json_to_extract;
      json_to_extract["tags"] = serialized_tags;
      json_to_extract["trace_id"] = "123";
      json_to_extract["parent_id"] = "456";
      std::istringstream to_extract(json_to_extract.dump());

      const auto maybe_context = tracer->Extract(to_extract);
      REQUIRE(maybe_context);
      const auto& context = maybe_context.value();
      REQUIRE(context);

      const auto span = tracer->StartSpan("OperationMoonUnit", {ot::ChildOf(context.get())});
      REQUIRE(span);

      std::ostringstream injected;
      const auto result = tracer->Inject(span->context(), injected);
      REQUIRE(result);

      const auto injected_json = nlohmann::json::parse(injected.str());
      REQUIRE(injected_json.find("tags") == injected_json.end());

      // Because the tags were omitted due to being oversized, there will be a
      // specific error tag on the local root span.
      span->Finish();

      REQUIRE(buffer->traces().size() == 1);
      const auto entry_iter = buffer->traces().begin();
      REQUIRE(entry_iter->first == 123);
      const auto& trace = entry_iter->second;
      REQUIRE(trace.finished_spans);
      REQUIRE(trace.finished_spans->size() == 1);
      const auto& maybe_finished_span = trace.finished_spans->front();
      REQUIRE(maybe_finished_span);
      const auto& finished_span = *maybe_finished_span;
      const auto found = finished_span.meta.find("_dd.propagation_error");
      REQUIRE(found != finished_span.meta.end());
      REQUIRE(found->second == "max_size");
    }
  }

  SECTION("can fail to decode; extraction continues with an error message") {
    const std::string serialized_tags = "_dd.p.upstream_services=dHJhY2Utc3RhdHMtcXVlcnk|2|bogus|";

    nlohmann::json json_to_extract;
    json_to_extract["tags"] = serialized_tags;
    json_to_extract["trace_id"] = "123";
    json_to_extract["parent_id"] = "456";
    std::istringstream to_extract(json_to_extract.dump());

    // Extraction succeeds.
    auto maybe_context = tracer->Extract(to_extract);
    REQUIRE(maybe_context);
    auto& context = maybe_context.value();
    REQUIRE(context);

    // An error was logged (about the bogus `serialized_tags`).
    REQUIRE(logger->records.size() == 1);
    const auto& log_record = logger->records[0];
    REQUIRE(log_record.level == LogLevel::error);
  }
}
