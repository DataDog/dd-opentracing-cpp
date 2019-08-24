#include "../src/tracer_options.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

void requireTracerOptionsResultsMatch(const ot::expected<TracerOptions, const char *> &lhs,
                                      const ot::expected<TracerOptions, const char *> &rhs) {
  // One is an error, the other not.
  REQUIRE((!!lhs) == (!!rhs));
  // They're both errors.
  if (!lhs) {
    REQUIRE(std::string(lhs.error()) == std::string(rhs.error()));
    return;
  }
  // Compare TracerOptions.
  REQUIRE(lhs->agent_host == rhs->agent_host);
  REQUIRE(lhs->agent_port == rhs->agent_port);
  REQUIRE(lhs->service == rhs->service);
  REQUIRE(lhs->type == rhs->type);
  REQUIRE(lhs->environment == rhs->environment);
  REQUIRE(lhs->sample_rate == rhs->sample_rate);
  REQUIRE(lhs->priority_sampling == rhs->priority_sampling);
  REQUIRE(lhs->write_period_ms == rhs->write_period_ms);
  REQUIRE(lhs->operation_name_override == rhs->operation_name_override);
  REQUIRE(lhs->extract == rhs->extract);
  REQUIRE(lhs->inject == rhs->inject);
};

TEST_CASE("tracer options from environment variables") {
  TracerOptions input{};
  struct TestCase {
    std::map<std::string, std::string> environment_variables;
    ot::expected<TracerOptions, const char *> expected;
  };

  auto test_case = GENERATE(values<TestCase>({
      {{}, TracerOptions{}},
      {{{"DD_AGENT_HOST", "host"},
        {"DD_TRACE_AGENT_PORT", "420"},
        {"DD_ENV", "env"},
        {"DD_PROPAGATION_STYLE_EXTRACT", "B3 Datadog"},
        {"DD_PROPAGATION_STYLE_INJECT", "Datadog B3"}},
       TracerOptions{"host",
                     420,
                     "",
                     "web",
                     "env",
                     1.0,
                     true,
                     1000,
                     "",
                     {PropagationStyle::Datadog, PropagationStyle::B3},
                     {PropagationStyle::Datadog, PropagationStyle::B3}}},
      {{{"DD_PROPAGATION_STYLE_EXTRACT", "Not even a real style"}},
       ot::make_unexpected("Value for DD_PROPAGATION_STYLE_EXTRACT is invalid")},
      {{{"DD_PROPAGATION_STYLE_INJECT", "Not even a real style"}},
       ot::make_unexpected("Value for DD_PROPAGATION_STYLE_INJECT is invalid")},
      {{{"DD_TRACE_AGENT_PORT", "sixty nine"}},
       ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is invalid")},
      {{{"DD_TRACE_AGENT_PORT", "9223372036854775807"}},
       ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is out of range")},
  }));

  // Setup
  for (const auto &env_var : test_case.environment_variables) {
    REQUIRE(setenv(env_var.first.c_str(), env_var.second.c_str(), 1) == 0);
  }

  auto got = applyTracerOptionsFromEnvironment(input);
  requireTracerOptionsResultsMatch(test_case.expected, got);

  // Teardown
  for (const auto &env_var : test_case.environment_variables) {
    REQUIRE(unsetenv(env_var.first.c_str()) == 0);
  }
}
