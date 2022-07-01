#include "../src/sample.h"

#include <algorithm>
#include <catch2/catch.hpp>
#include <ctime>
#include <nlohmann/json.hpp>

#include "../src/agent_writer.h"
#include "../src/span.h"
#include "../src/tracer.h"
#include "mocks.h"
using namespace datadog::opentracing;
using json = nlohmann::json;

TEST_CASE("priority sampler unit test") {
  PrioritySampler sampler;
  auto buffer = std::make_shared<MockBuffer>();

  SECTION("default unconfigured priority sampling behaviour is to always sample") {
    auto result = sampler.sample("", "", 0);
    REQUIRE(result.priority_rate == 1.0);
    REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
    result = sampler.sample("env", "service", 1);
    REQUIRE(result.priority_rate == 1.0);
    REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
  }

  SECTION("configured") {
    sampler.configure("{ \"service:nginx,env:\": 0.8, \"service:nginx,env:prod\": 0.2 }"_json);

    SECTION("spans that don't match a rule use the default rate") {
      auto result = sampler.sample("different env", "different service", 1);
      REQUIRE(result.priority_rate == 1.0);
      REQUIRE(*result.sampling_priority == SamplingPriority::SamplerKeep);
    }

    SECTION("spans can be sampled") {
      // Case 1, service:nginx,env: => 0.8
      int count_sampled = 0;
      int total = 10000;
      for (int i = 0; i < total; i++) {
        auto result = sampler.sample("", "nginx", getId());
        const auto& p = result.sampling_priority;

        REQUIRE(p != nullptr);
        REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
        count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
      }
      double sample_rate = count_sampled / static_cast<double>(total);
      REQUIRE((sample_rate < 0.85 && sample_rate > 0.75));
      // Case 2, service:nginx,env:prod => 0.2
      count_sampled = 0;
      total = 10000;
      for (int i = 0; i < total; i++) {
        auto result = sampler.sample("", "nginx", getId());
        const auto& p = result.sampling_priority;
        REQUIRE(p != nullptr);
        REQUIRE(((*p == SamplingPriority::SamplerKeep) || (*p == SamplingPriority::SamplerDrop)));
        count_sampled += *p == SamplingPriority::SamplerKeep ? 1 : 0;
      }
      sample_rate = count_sampled / static_cast<double>(total);
      REQUIRE((sample_rate < 0.85 && sample_rate > 0.75));
    }
  }
}

TEST_CASE("rules sampler") {
  // `RulesSampler`'s constructor parameters are used to configure the
  // sampler's `Limiter`. Here we prepare those arguments.
  std::tm start{};
  start.tm_mday = 12;
  start.tm_mon = 2;
  start.tm_year = 107;
  const TimePoint time{std::chrono::system_clock::from_time_t(timegm(&start)),
                       std::chrono::steady_clock::time_point{}};
  const TimeProvider get_time = [&time]() { return time; };  // Mock clock.
  // A `Limiter` configured with these parameters will allow the first, but
  // none afterward.
  const long max_tokens = 1;
  const double refresh_rate = 1.0;
  const long tokens_per_refresh = 1;
  const auto sampler =
      std::make_shared<RulesSampler>(get_time, max_tokens, refresh_rate, tokens_per_refresh);

  const ot::StartSpanOptions span_options;
  const ot::FinishSpanOptions finish_options;

  const auto mwriter = std::make_shared<MockWriter>(sampler);
  const auto writer = std::shared_ptr<Writer>(mwriter);

  SECTION("rule matching applied") {
    TracerOptions tracer_options;
    tracer_options.service = "test.service";
    tracer_options.sampling_rules = R"([
    {"name": "test.trace", "service": "test.service", "sample_rate": 0.1},
    {"name": "name.only.match", "sample_rate": 0.2},
    {"service": "service.only.match", "sample_rate": 0.3},
    {"name": "overridden operation name", "sample_rate": 0.4},
    {"sample_rate": 1.0}
])";
    auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());
    struct RulesSamplerTestCase {
      std::string service;
      std::string name;
      bool matched;
      double rate;
    };
    auto test_case = GENERATE(values<RulesSamplerTestCase>({
        {"test.service", "test.trace", true, 0.1},
        {"any.service", "name.only.match", true, 0.2},
        {"service.only.match", "any.name", true, 0.3},
        {"any.service", "any.name", true, 1.0},
    }));
    auto result = sampler->match(test_case.service, test_case.name);
    REQUIRE(test_case.matched == result.matched);
    if (std::isnan(test_case.rate)) {
      REQUIRE(std::isnan(result.rate));
    } else {
      REQUIRE(test_case.rate == result.rate);
    }
  }

  SECTION("falls back to priority sampling when no matching rule") {
    TracerOptions tracer_options;
    tracer_options.service = "test.service";
    // In addition to `tracer_options.sampling_rules`, there would be an
    // implicit rule added if `tracer_options.sample_rate` were not NaN.  That
    // case is handled in the next section (not this one).
    tracer_options.sampling_rules = R"([
    {"name": "unmatched.name", "service": "unmatched.service", "sample_rate": 0.1}
])";
    auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());

    auto span = tracer->StartSpanWithOptions("operation.name", span_options);
    span->FinishWithOptions(finish_options);

    auto& metrics = mwriter->traces[0][0]->metrics;
    REQUIRE(metrics.find("_dd.rule_psr") == metrics.end());
    REQUIRE(metrics.find("_dd.limit_psr") == metrics.end());
    REQUIRE(metrics.find("_dd.agent_psr") != metrics.end());
  }

  SECTION("falls back to catch-all rule if sample_rate is configured and no other rule matches") {
    TracerOptions tracer_options;
    tracer_options.service = "test.service";
    tracer_options.sample_rate = 0.5;
    tracer_options.sampling_rules = R"([
    {"name": "unmatched.name", "service": "unmatched.service", "sample_rate": 0.1}
])";
    auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());

    auto span = tracer->StartSpanWithOptions("operation.name", span_options);
    span->FinishWithOptions(finish_options);

    auto& metrics = mwriter->traces[0][0]->metrics;
    REQUIRE(metrics.find("_dd.rule_psr") != metrics.end());
    REQUIRE(metrics["_dd.rule_psr"] == 0.5);
    REQUIRE(metrics.find("_dd.agent_psr") == metrics.end());
  }

  SECTION("rule matching applied to overridden name") {
    TracerOptions tracer_options;
    tracer_options.service = "test.service";
    tracer_options.sampling_rules = R"([
    {"name": "overridden operation name", "sample_rate": 0.4},
    {"sample_rate": 1.0}
])";
    tracer_options.operation_name_override = "overridden operation name";
    auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());

    auto span = tracer->StartSpanWithOptions("operation name", span_options);
    span->FinishWithOptions(finish_options);

    auto& metrics = mwriter->traces[0][0]->metrics;
    REQUIRE(metrics.find("_dd.rule_psr") != metrics.end());
    REQUIRE(metrics["_dd.rule_psr"] == 0.4);
  }

  SECTION("applies limiter to sampled spans only") {
    TracerOptions tracer_options;
    tracer_options.service = "test.service";
    tracer_options.sampling_rules = R"([
    {"sample_rate": 0.0}
])";
    auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());

    auto span = tracer->StartSpanWithOptions("operation name", span_options);
    span->FinishWithOptions(finish_options);

    auto& metrics = mwriter->traces[0][0]->metrics;
    REQUIRE(metrics.find("_dd.rule_psr") != metrics.end());
    REQUIRE(metrics["_dd.rule_psr"] == 0.0);
    REQUIRE(metrics.find("_dd.limit_psr") == metrics.end());
    REQUIRE(metrics.find("_dd.agent_psr") == metrics.end());
  }

  SECTION("sampling based on rule yields a 'user' sampling priority") {
    // See the comments in `RulesSampler::sample` for an explanation of this
    // section.

    // There are three cases:
    // 1. Create a rule that matches the trace, and has rate `0.0`. Expect
    //     priority `UserDrop`.
    // 2. Create a rule that matches the trace, and has rate `1.0`. Expect
    //     priority `UserKeep`.
    // 3. Create a rule that matches the trace, and has rate `1.0`, but the
    //     limiter drops it. Expect `UserDrop`.

    SECTION("when the matching rule drops a trace") {
      TracerOptions tracer_options;
      tracer_options.service = "test.service";
      tracer_options.sampling_rules = R"([
    {"sample_rate": 0.0}
])";
      const auto tracer = std::make_shared<Tracer>(tracer_options, writer, sampler,
                                                   std::make_shared<MockLogger>());

      const auto span = tracer->StartSpanWithOptions("operation name", span_options);
      span->FinishWithOptions(finish_options);

      REQUIRE(mwriter->traces.size() == 1);
      REQUIRE(mwriter->traces[0].size() == 1);
      const auto& metrics = mwriter->traces[0][0]->metrics;
      REQUIRE(metrics.count("_sampling_priority_v1"));
      REQUIRE(metrics.at("_sampling_priority_v1") ==
              static_cast<double>(SamplingPriority::UserDrop));
    }

    SECTION("when the matching rule keeps a trace") {
      TracerOptions tracer_options;
      tracer_options.service = "test.service";
      tracer_options.sampling_rules = R"([
    {"sample_rate": 1.0}
])";
      const auto tracer = std::make_shared<Tracer>(tracer_options, writer, sampler,
                                                   std::make_shared<MockLogger>());

      const auto span = tracer->StartSpanWithOptions("operation name", span_options);
      span->FinishWithOptions(finish_options);

      REQUIRE(mwriter->traces.size() == 1);
      REQUIRE(mwriter->traces[0].size() == 1);
      const auto& metrics = mwriter->traces[0][0]->metrics;
      REQUIRE(metrics.count("_sampling_priority_v1"));
      REQUIRE(metrics.at("_sampling_priority_v1") ==
              static_cast<double>(SamplingPriority::UserKeep));
    }

    SECTION("when the limiter drops a trace") {
      TracerOptions tracer_options;
      tracer_options.service = "test.service";
      tracer_options.sampling_rules = R"([
    {"sample_rate": 1.0}
])";
      const auto tracer = std::make_shared<Tracer>(tracer_options, writer, sampler,
                                                   std::make_shared<MockLogger>());

      // The first span will be allowed by the limiter (tested in the previous section).
      auto span = tracer->StartSpanWithOptions("operation name", span_options);
      span->FinishWithOptions(finish_options);

      // The second trace will be dropped by the limiter, and the priority will
      // be `UserDrop`.
      span = tracer->StartSpanWithOptions("operation name", span_options);
      span->FinishWithOptions(finish_options);
      {
        REQUIRE(mwriter->traces.size() == 2);
        REQUIRE(mwriter->traces[1].size() == 1);
        const auto& metrics = mwriter->traces[1][0]->metrics;
        REQUIRE(metrics.count("_sampling_priority_v1"));
        REQUIRE(metrics.at("_sampling_priority_v1") ==
                static_cast<double>(SamplingPriority::UserDrop));
      }
    }
  }

  SECTION("reports 'rule' sampling mechanism") {
    TracerOptions tracer_options;
    tracer_options.service = "zappasvc";
    tracer_options.sampling_rules = R"([
    {"sample_rate": 1.0}
])";
    const auto tracer =
        std::make_shared<Tracer>(tracer_options, writer, sampler, std::make_shared<MockLogger>());

    const auto span = tracer->StartSpanWithOptions("OperationMoonUnit", span_options);
    span->FinishWithOptions(finish_options);

    // The `SpanBuffer` will have made a sampling decision based on the
    // matching rule, and the resulting `SamplingMechanism` will be visible in
    // the "_dd.p.dm" tag.
    //
    // The expectation is that, since sampling was performed on account of a
    // sampling rule, the sampling mechanism will be
    // `SamplingMechanism::Rule` (which is 3).

    REQUIRE(mwriter->traces.size() == 1);
    REQUIRE(mwriter->traces[0].size() == 1);
    const auto& maybe_span = mwriter->traces[0][0];
    REQUIRE(maybe_span);
    const auto& span_data = *maybe_span;

    const auto tag_found = span_data.meta.find("_dd.p.dm");
    REQUIRE(tag_found != span_data.meta.end());
    const std::string& decision_maker = tag_found->second;
    const std::string expected = "-" + std::to_string(int(SamplingMechanism::Rule));
    REQUIRE(decision_maker == expected);
  }
}

TEST_CASE("SpanSampler rule parsing") {
  MockLogger logger;
  const auto dummy_clock = []() { return TimePoint(); };
  SpanSampler sampler;

  SECTION("empty array means no rules") {
    sampler.configure("[]", logger, dummy_clock);
    REQUIRE(sampler.rules().size() == 0);
    REQUIRE(logger.records.size() == 0);
  }

  SECTION("default values for rule properties") {
    sampler.configure("[{}]", logger, dummy_clock);
    REQUIRE(sampler.rules().size() == 1);
    REQUIRE(logger.records.size() == 0);

    const SpanSampler::Rule::Config& config = sampler.rules().front().config();
    CAPTURE(config.max_per_second);
    REQUIRE(std::isnan(config.max_per_second));
    REQUIRE(config.operation_name_pattern == "*");
    REQUIRE(config.sample_rate == 1.0);
    REQUIRE(config.service_pattern == "*");
    REQUIRE(config.text == "{}");
  }

  SECTION("valid values for rule properties") {
    const std::string rule_json = R"json({
      "service": "foosvc",
      "name": "handle.thing",
      "sample_rate": 0.1,
      "max_per_second": 1000
    })json";
    sampler.configure("[" + rule_json + "]", logger, dummy_clock);
    REQUIRE(sampler.rules().size() == 1);
    REQUIRE(logger.records.size() == 0);

    const SpanSampler::Rule::Config& config = sampler.rules().front().config();
    REQUIRE(config.max_per_second == 1000);
    REQUIRE(config.operation_name_pattern == "handle.thing");
    REQUIRE(config.sample_rate == 0.1);
    REQUIRE(config.service_pattern == "foosvc");
    REQUIRE(json::parse(config.text) == json::parse(rule_json));
  }

  SECTION("invalid JSON yields no rules and logs error") {
    auto bad_json = GENERATE(as<ot::string_view>{}, "this is not json", "[{'neither': 'is this'}]",
                             "[{}, {4}, {}]");

    CAPTURE(bad_json);
    sampler.configure(bad_json, logger, dummy_clock);
    REQUIRE(sampler.rules().size() == 0);
    REQUIRE(logger.records.size() == 1);
    const auto& log_record = logger.records.front();
    REQUIRE(log_record.level == LogLevel::error);
    CAPTURE(log_record.message);
    REQUIRE(log_record.message.find("JSON") != std::string::npos);
  }

  SECTION("invalid rules are skipped and log error") {
    struct TestCase {
      ot::string_view rules_json;
      unsigned expected_rule_count;
      ot::string_view expected_error_excerpt;
    };

    auto test_case = GENERATE(values<TestCase>(
        {// "sample_rate" has the wrong type
         {R"json([{"sample_rate": "foo"}, {}, {}, {}])json", 3, "sample_rate"},
         // "sample_rate" is out of range
         {R"json([{"sample_rate": 1.2}, {}, {}, {}])json", 3, "sample_rate"},
         // "max_per_second" has the wrong type
         {R"json([{}, {"max_per_second": null}, {}, {}])json", 3, "max_per_second"},
         // "max_per_second" is out of range
         {R"json([{}, {"max_per_second": 0}, {}, {}])json", 3, "max_per_second"},
         // "service" has the wrong type
         {R"json([{}, {}, {"service": 10}, {}])json", 3, "service"},
         // "name" has the wrong type
         {R"json([{}, {}, {}, {"name": false}])json", 3, "name"}}));

    CAPTURE(test_case.rules_json);
    sampler.configure(test_case.rules_json, logger, dummy_clock);

    REQUIRE(sampler.rules().size() == test_case.expected_rule_count);

    REQUIRE(logger.records.size() == 1);
    const auto& log_record = logger.records.front();
    CAPTURE(log_record.message);
    REQUIRE(log_record.level == LogLevel::error);

    CAPTURE(test_case.expected_error_excerpt);
    REQUIRE(log_record.message.find(test_case.expected_error_excerpt) != std::string::npos);
  }
}

namespace {

// `join({"x", "y", "z"}, " -> ") == "x -> y -> z"`
std::string join(const std::vector<std::string>& pieces, ot::string_view separator) {
  std::string result;
  auto iter = pieces.begin();
  const auto end = pieces.end();
  if (iter != end) {
    result += *iter;
    for (++iter; iter != end; ++iter) {
      result.append(separator.data(), separator.size());
      result += *iter;
    }
  }
  return result;
}

}  // namespace

TEST_CASE("SpanSampler matching") {
  MockLogger logger;
  const auto dummy_clock = []() { return TimePoint(); };
  SpanSampler sampler;

  const std::vector<std::string> json_rules = {
      R"json({"service": "mysql", "name": "exec.*", "sample_rate": 1.0})json",
      R"json({"service": "mysql*", "sample_rate": 0.1})json",
      R"json({"name": "super.auth", "sample_rate": 1.0})json",
      R"json({"name": "super.auth??", "sample_rate": 1.0})json",
  };

  sampler.configure("[" + join(json_rules, ", ") + "]", logger, dummy_clock);
  REQUIRE(logger.records.size() == 0);
  REQUIRE(sampler.rules().size() == json_rules.size());

  SECTION("span can match multiple rules, but the first matching rule is chosen") {
    SpanData span;
    span.service = "mysql";
    span.name = "exec.query";
    // `span` matches both `json_rules[0]` and `json_rules[1]`, but the earlier
    // rule will be chosen (`json_rules[0]`).

    // First check that two rules could match.
    const unsigned match_count =
        std::count_if(sampler.rules().begin(), sampler.rules().end(),
                      [&](const SpanSampler::Rule& rule) { return rule.match(span); });
    REQUIRE(match_count == 2);

    // Then check that the first one is chosen.
    SpanSampler::Rule* rule = sampler.match(span);
    REQUIRE(rule == &sampler.rules()[0]);
  }

  SECTION("no match") {
    SpanData span;
    span.service = "table";
    span.name = "check.please";
    REQUIRE(sampler.match(span) == nullptr);
  }

  SECTION("match by service name") {
    SpanData span;
    span.service = "mysql123";
    span.name = "cache.lookup";
    // matches `json_rules[1]`
    REQUIRE(sampler.match(span) == &sampler.rules()[1]);
  }

  SECTION("match by operation name") {
    SpanData span;
    span.service = "langley";
    span.name = "super.auth";
    // matches `json_rules[2]`
    REQUIRE(sampler.match(span) == &sampler.rules()[2]);
  }

  SECTION("match by service name and operation name") {
    SpanData span;
    span.service = "mysql";
    span.name = "exec.query";
    // matches `json_rules[0]` (as before)
    SpanSampler::Rule* rule = sampler.match(span);
    REQUIRE(rule == &sampler.rules()[0]);
  }

  SECTION("match involving question marks") {
    SpanData span;
    span.service = "roswell";
    span.name = "super.auth51";
    // matches `json_rules[3]` (not `json_rules[2]`)
    REQUIRE(sampler.match(span) == &sampler.rules()[3]);
  }
}

TEST_CASE("SpanSampler sampling") {
  // Starting calendar time 2022-07-01 00:00:00 local time
  std::tm start{};
  start.tm_year = 122;
  start.tm_mon = 7;
  start.tm_mday = 1;
  TimePoint now{std::chrono::system_clock::from_time_t(std::mktime(&start)),
                std::chrono::steady_clock::time_point{}};
  // Note: Use `advanceTime(now, ...)` to advance the clock.
  const auto clock = [&now]() { return now; };
  std::uint64_t next_id = 1;
  const auto make_id = [&next_id]() { return next_id++; };
  const auto trace_sampler = std::make_shared<MockRulesSampler>();
  const auto writer = std::make_shared<MockWriter>(trace_sampler);
  const auto span_sampler = std::make_shared<SpanSampler>();
  const auto logger = std::make_shared<MockLogger>();
  const auto span_buffer = std::make_shared<SpanBuffer>(logger, writer, trace_sampler,
                                                        span_sampler, SpanBufferOptions{});
  TracerOptions tracer_options;
  tracer_options.service = "foosvc";
  const auto tracer = std::make_shared<Tracer>(tracer_options, span_buffer, clock, make_id, logger);
  const auto has_span_sampling_tag = [](const auto& span_ptr) {
    const auto& numeric_tags = span_ptr->metrics;
    return numeric_tags.count("_dd.span_sampling.mechanism") ||
      numeric_tags.count("_dd.span_sampling.rule_rate") ||
      numeric_tags.count("_dd.span_sampling.max_per_second");
  };

  SECTION("no span_sampling tags when there are no span sampling rules") {
    // Make sure that the trace sampler is dropping the trace, otherwise we
    // wouldn't expect the span sampling rules to matter.
    trace_sampler->rule_rate = 0;
    trace_sampler->sampling_mechanism = SamplingMechanism::Manual;
    trace_sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);

    // We expect an empty array of rules to mean that span sampling won't
    // happen.
    span_sampler->configure("[]", *logger, clock);
    {
      const auto root = tracer->StartSpan("root");
      REQUIRE(root);
      advanceTime(now, std::chrono::milliseconds(8));
      const auto child1 = tracer->StartSpan("child1", {ot::ChildOf(&root->context())});
      REQUIRE(child1);
      advanceTime(now, std::chrono::milliseconds(5));
      const auto child2 = tracer->StartSpan("child2", {ot::ChildOf(&root->context())});
      REQUIRE(child2);
      advanceTime(now, std::chrono::milliseconds(31));
      const auto grandchild = tracer->StartSpan("grandchild", {ot::ChildOf(&child1->context())});
      REQUIRE(grandchild);
      advanceTime(now, std::chrono::milliseconds(2));
    }

    REQUIRE(writer->traces.size() == 1);
    const auto& trace = writer->traces.front();
    REQUIRE(trace.size() == 4);

    REQUIRE(std::none_of(trace.begin(), trace.end(), has_span_sampling_tag));
  }

  SECTION("no span_sampling tags when the trace is kept") {
    // When the trace is kept, span sampling rules aren't consulted (even if
    // they would match and keep spans).
    trace_sampler->rule_rate = 1;
    trace_sampler->sampling_mechanism = SamplingMechanism::Manual;
    trace_sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserKeep);

    const auto rules_json = R"json([
      {"service": "foosvc", "name": "grandchild"},
      {"name": "child*"},
      {"service": "foosvc", "max_per_second": 1000}
    ])json";
    span_sampler->configure(rules_json, *logger, clock);
    {
      const auto root = tracer->StartSpan("root");
      REQUIRE(root);
      advanceTime(now, std::chrono::milliseconds(8));
      const auto child1 = tracer->StartSpan("child1", {ot::ChildOf(&root->context())});
      REQUIRE(child1);
      advanceTime(now, std::chrono::milliseconds(5));
      const auto child2 = tracer->StartSpan("child2", {ot::ChildOf(&root->context())});
      REQUIRE(child2);
      advanceTime(now, std::chrono::milliseconds(31));
      const auto grandchild = tracer->StartSpan("grandchild", {ot::ChildOf(&child1->context())});
      REQUIRE(grandchild);
      advanceTime(now, std::chrono::milliseconds(2));
    }

    REQUIRE(writer->traces.size() == 1);
    const auto& trace = writer->traces.front();
    REQUIRE(trace.size() == 4);

    REQUIRE(std::none_of(trace.begin(), trace.end(), has_span_sampling_tag));
  }

  SECTION("expected span_sampling tags when the trace is dropped") {
    // Make sure that the trace sampler is dropping the trace, otherwise we
    // wouldn't expect the span sampling rules to matter.
    trace_sampler->rule_rate = 0;
    trace_sampler->sampling_mechanism = SamplingMechanism::Manual;
    trace_sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);

    const auto rules_json = R"json([
      {"service": "foosvc", "name": "grandchild", "max_per_second": 999},
      {"name": "child*"},
      {"service": "foosvc", "max_per_second": 1000}
    ])json";
    span_sampler->configure(rules_json, *logger, clock);
    {
      const auto root = tracer->StartSpan("root");
      REQUIRE(root);
      advanceTime(now, std::chrono::milliseconds(8));
      const auto child1 = tracer->StartSpan("child1", {ot::ChildOf(&root->context())});
      REQUIRE(child1);
      advanceTime(now, std::chrono::milliseconds(5));
      const auto child2 = tracer->StartSpan("child2", {ot::ChildOf(&root->context())});
      REQUIRE(child2);
      advanceTime(now, std::chrono::milliseconds(31));
      const auto grandchild = tracer->StartSpan("grandchild", {ot::ChildOf(&child1->context())});
      REQUIRE(grandchild);
      advanceTime(now, std::chrono::milliseconds(2));
    }

    REQUIRE(writer->traces.size() == 1);
    const auto& trace = writer->traces.front();
    REQUIRE(trace.size() == 4);

    for (const auto& span_ptr : trace) {
      const auto& span = *span_ptr;
      const auto& numeric_tags = span.metrics;
      if (span.name == "root") {
        // `root` matches the rule: {"service": "foosvc", "max_per_second": 1000}
        REQUIRE(numeric_tags.count("_dd.span_sampling.mechanism") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.mechanism") == int(SamplingMechanism::SpanRule));
        REQUIRE(numeric_tags.count("_dd.span_sampling.rule_rate") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.rule_rate") == 1.0);
        REQUIRE(numeric_tags.count("_dd.span_sampling.max_per_second") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.max_per_second") == 1000);
      } else if (span.name == "child1" || span.name == "child2") {
        // `child1` and `child2` match the rule: {"name": "child*"}
        REQUIRE(numeric_tags.count("_dd.span_sampling.mechanism") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.mechanism") == int(SamplingMechanism::SpanRule));
        REQUIRE(numeric_tags.count("_dd.span_sampling.rule_rate") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.rule_rate") == 1.0);
        REQUIRE(numeric_tags.count("_dd.span_sampling.max_per_second") == 0);
      } else {
        REQUIRE(span.name == "grandchild");
        // `grandchild` matches the rule: {"service": "foosvc", "name": "grandchild"}
        REQUIRE(numeric_tags.count("_dd.span_sampling.mechanism") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.mechanism") == int(SamplingMechanism::SpanRule));
        REQUIRE(numeric_tags.count("_dd.span_sampling.rule_rate") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.rule_rate") == 1.0);
        REQUIRE(numeric_tags.count("_dd.span_sampling.max_per_second") == 1);
        REQUIRE(numeric_tags.at("_dd.span_sampling.max_per_second") == 999);
      }
    }
  }

  SECTION("probabilistic sampling for span rules") {
    // Make sure that the trace sampler is dropping the trace, otherwise we
    // wouldn't expect the span sampling rules to matter.
    trace_sampler->rule_rate = 0;
    trace_sampler->sampling_mechanism = SamplingMechanism::Manual;
    trace_sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);

    const auto rules_json = R"json([
      {"name": "mysql.*", "sample_rate": 0.5}
    ])json";
    span_sampler->configure(rules_json, *logger, clock);
    const int child_count = 10000;
    {
      const auto root = tracer->StartSpan("root");
      REQUIRE(root);
      advanceTime(now, std::chrono::milliseconds(8));
      // Generate a lot of spans that match the rule, so that our 50%
      // probability can be measured.
      for (int i = 0; i < child_count; ++i) {
        const auto child = tracer->StartSpan("mysql.query", {ot::ChildOf(&root->context())});
        REQUIRE(child);
        advanceTime(now, std::chrono::milliseconds(100));
      }
    }

    REQUIRE(writer->traces.size() == 1);
    const auto& trace = writer->traces.front();
    REQUIRE(trace.size() == child_count + 1);

    // `root` did not match any rule.
    const auto root_iter = std::find_if(trace.begin(), trace.end(), [](const auto& span_ptr) { return span_ptr->name == "root"; });
    REQUIRE(root_iter != trace.end());
    REQUIRE(!has_span_sampling_tag(*root_iter));

    const int kept_children_count = std::count_if(trace.begin(), trace.end(), has_span_sampling_tag);
    // 50% of `child_count` would be 5000.  Let's say within 5% of that -> 5000 +/- 10.
    REQUIRE(kept_children_count >= 5000 - 10);
    REQUIRE(kept_children_count <= 5000 + 10);
  }

  SECTION("rate limiting for span rules") {
    // Make sure that the trace sampler is dropping the trace, otherwise we
    // wouldn't expect the span sampling rules to matter.
    trace_sampler->rule_rate = 0;
    trace_sampler->sampling_mechanism = SamplingMechanism::Manual;
    trace_sampler->sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);

    const auto rules_json = R"json([
      {"name": "mysql.*", "max_per_second": 10}
    ])json";
    span_sampler->configure(rules_json, *logger, clock);

    // Each trace's 20 children will hit the limiter after 10.
    // Then a second goes by, so the limiter recharges.
    // We'd expect 10 kept per trace, or 1000 total.
    const int num_traces = 100;
    const int children_per_trace = 20;
    const int milliseconds_between_traces = 1000;
    {
      for (int i = 0; i < num_traces; ++i) {
        advanceTime(now, std::chrono::milliseconds(milliseconds_between_traces));
        const auto root = tracer->StartSpan("root");
        REQUIRE(root);
        for (int j = 0; j < children_per_trace; ++j) {
          const auto child = tracer->StartSpan("mysql.query", {ot::ChildOf(&root->context())});
          REQUIRE(child);
        }
      }
    }

    REQUIRE(writer->traces.size() == num_traces);

    int kept_spans_count = 0;
    for (const auto& trace : writer->traces) {
      kept_spans_count += std::count_if(trace.begin(), trace.end(), has_span_sampling_tag);
    }

    // 10 is the configured `max_per_second`.
    REQUIRE(kept_spans_count == num_traces * 10);
  }
}
