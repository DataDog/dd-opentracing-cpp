#include "../src/tracer_options.h"

#include <opentracing/ext/tags.h>

#include <catch2/catch.hpp>
#include <ostream>
using namespace datadog::opentracing;
using namespace std::string_literals;

namespace datadog {
namespace opentracing {

std::ostream &operator<<(std::ostream &stream,
                         const ot::expected<TracerOptions, std::string> &result) {
  if (result.has_value()) {
    return stream << "TracerOptions{...}";
  }
  return stream << result.error();
}

}  // namespace opentracing
}  // namespace datadog

namespace std {

std::ostream &operator<<(std::ostream &stream,
                         const std::pair<const std::string, std::string> &key_value) {
  return stream << key_value.first << " → " << key_value.second;
}

}  // namespace std

void requireTracerOptionsResultsMatch(const ot::expected<TracerOptions, std::string> &lhs,
                                      const ot::expected<TracerOptions, std::string> &rhs) {
  // One is an error, the other not.
  REQUIRE((!!lhs) == (!!rhs));
  // They're both errors.
  if (!lhs) {
    REQUIRE(lhs.error() == rhs.error());
    return;
  }
  // Compare TracerOptions.
  REQUIRE(lhs->agent_host == rhs->agent_host);
  REQUIRE(lhs->agent_port == rhs->agent_port);
  REQUIRE(lhs->service == rhs->service);
  REQUIRE(lhs->type == rhs->type);
  REQUIRE(lhs->environment == rhs->environment);
  if (std::isnan(lhs->sample_rate)) {
    REQUIRE(std::isnan(rhs->sample_rate));
  } else {
    REQUIRE(lhs->sample_rate == rhs->sample_rate);
  }
  REQUIRE(lhs->priority_sampling == rhs->priority_sampling);
  REQUIRE(lhs->sampling_rules == rhs->sampling_rules);
  REQUIRE(lhs->write_period_ms == rhs->write_period_ms);
  REQUIRE(lhs->operation_name_override == rhs->operation_name_override);
  REQUIRE(lhs->extract == rhs->extract);
  REQUIRE(lhs->inject == rhs->inject);
  REQUIRE(lhs->report_hostname == rhs->report_hostname);
  REQUIRE(lhs->analytics_enabled == rhs->analytics_enabled);
  if (std::isnan(lhs->analytics_rate)) {
    REQUIRE(std::isnan(rhs->analytics_rate));
  } else {
    REQUIRE(lhs->analytics_rate == rhs->analytics_rate);
  }
  REQUIRE(lhs->tags == rhs->tags);
  REQUIRE(lhs->sampling_limit_per_second == rhs->sampling_limit_per_second);
}

TEST_CASE("tracer options from environment variables") {
  TracerOptions input{};
  struct TestCase {
    std::map<std::string, std::string> environment_variables;
    ot::expected<TracerOptions, std::string> expected;
  };

  auto test_case = GENERATE(values<TestCase>({
      {{}, TracerOptions{}},
      {{{"DD_AGENT_HOST", "host"},
        {"DD_TRACE_AGENT_PORT", "420"},
        {"DD_ENV", "test-env"},
        {"DD_SERVICE", "service"},
        {"DD_VERSION", "test-version v0.0.1"},
        {"DD_TRACE_SAMPLING_RULES", "rules"},
        {"DD_PROPAGATION_STYLE_EXTRACT", "B3 Datadog"},
        {"DD_PROPAGATION_STYLE_INJECT", "Datadog B3"},
        {"DD_TRACE_REPORT_HOSTNAME", "true"},
        {"DD_TRACE_ANALYTICS_ENABLED", "true"},
        {"DD_TRACE_ANALYTICS_SAMPLE_RATE", "0.5"},
        {"DD_TAGS", "host:my-host-name,region:us-east-1,datacenter:us,a.b_c:a:b,partition:5"},
        {"DD_TRACE_RATE_LIMIT", "200"},
        {"DD_TRACE_SAMPLE_RATE", "0.7"}},
       TracerOptions{
           "host",                                             // agent_host
           420,                                                // agent_port
           "service",                                          // service
           "web",                                              // type
           "test-env",                                         // environment
           0.7,                                                // sample_rate
           true,                                               // priority_sampling
           "rules",                                            // sampling_rules
           1000,                                               // write_period_ms
           "",                                                 // operation_name_override
           {PropagationStyle::Datadog, PropagationStyle::B3},  // extract
           {PropagationStyle::Datadog, PropagationStyle::B3},  // inject
           true,                                               // report_hostname
           true,                                               // analytics_enabled
           0.5,                                                // analytics_rate
           {
               {"host", "my-host-name"},
               {"region", "us-east-1"},
               {"datacenter", "us"},
               {"a.b_c", "a:b"},
               {"partition", "5"},
           },                                 // tags
           "test-version v0.0.1",             // version
           "",                                // agent_url
           [](LogLevel, ot::string_view) {},  // log_func (dummy)
           200                                // sampling_limit_per_second
       }},
      {{{"DD_PROPAGATION_STYLE_EXTRACT", "Not even a real style"}},
       ot::make_unexpected("Value for DD_PROPAGATION_STYLE_EXTRACT is invalid"s)},
      {{{"DD_PROPAGATION_STYLE_INJECT", "Not even a real style"}},
       ot::make_unexpected("Value for DD_PROPAGATION_STYLE_INJECT is invalid"s)},
      {{{"DD_TRACE_AGENT_PORT", "sixty nine"}},
       ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is invalid"s)},
      {{{"DD_TRACE_AGENT_PORT", "9223372036854775807"}},
       ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is out of range"s)},
      {{{"DD_TRACE_REPORT_HOSTNAME", "yes please"}},
       ot::make_unexpected("Value for DD_TRACE_REPORT_HOSTNAME is invalid"s)},
      {{{"DD_TRACE_ANALYTICS_ENABLED", "yes please"}},
       ot::make_unexpected("Value for DD_TRACE_ANALYTICS_ENABLED is invalid"s)},
      {{{"DD_TRACE_ANALYTICS_SAMPLE_RATE", "1.1"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_ANALYTICS_SAMPLE_RATE: not within the expected bounds [0, 1]: 1.1"s)},
      // Each of DD_TRACE_SAMPLE_RATE and DD_TRACE_RATE_LIMIT can fail to parse
      // for any of the following reasons:
      // - not a floating point number
      // - can't fit in a double
      // - outside of the expected range (e.g. [0, 1] for DD_TRACE_SAMPLE_RATE)
      // - has non-whitespace trailing characters
      {{{"DD_TRACE_SAMPLE_RATE", "total nonsense"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_SAMPLE_RATE: does not look like a double: total nonsense"s)},
      {{{"DD_TRACE_SAMPLE_RATE", "3.14e99999"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_SAMPLE_RATE: not within the range of a double: 3.14e99999"s)},
      {{{"DD_TRACE_SAMPLE_RATE", "1.6"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_SAMPLE_RATE: not within the expected bounds [0, 1]: 1.6"s)},
      {{{"DD_TRACE_SAMPLE_RATE", "NaN"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_SAMPLE_RATE: not within the expected bounds [0, 1]: nan"s)},
      {{{"DD_TRACE_SAMPLE_RATE", "0.5 BOOM"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_SAMPLE_RATE: contains trailing non-floating-point characters: 0.5 BOOM"s)},
      {{{"DD_TRACE_RATE_LIMIT", "total nonsense"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_RATE_LIMIT: does not look like a double: total nonsense"s)},
      {{{"DD_TRACE_RATE_LIMIT", "3.14e99999"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_RATE_LIMIT: not within the range of a double: 3.14e99999"s)},
      {{{"DD_TRACE_RATE_LIMIT", "-8"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_RATE_LIMIT: not within the expected bounds [0, inf]: -8"s)},
      {{{"DD_TRACE_RATE_LIMIT", "NaN"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_RATE_LIMIT: not within the expected bounds [0, inf]: nan"s)},
      {{{"DD_TRACE_RATE_LIMIT", "0.5 BOOM"}},
       ot::make_unexpected(
           "while parsing DD_TRACE_RATE_LIMIT: contains trailing non-floating-point characters: 0.5 BOOM"s)},
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

TEST_CASE("exceptions for DD_TAGS") {
  //
  TracerOptions input{};
  struct TestCase {
    std::map<std::string, std::string> environment_variables;
    std::map<std::string, std::string> tags;
  };

  auto test_case = GENERATE(values<TestCase>({
      {{{"DD_ENV", "foo"}, {"DD_TAGS", "env:bar"}}, {}},
      {{{"DD_TAGS", std::string(ot::ext::sampling_priority) + ":1"}}, {}},
      {{{"DD_TAGS", ":,:,:,:"}}, {}},  // repeatedly handles missing keys
      {{{"DD_TAGS", "keywithoutvalue:"}}, {}},
      {{{"DD_VERSION", "awesomeapp v1.2.3"}, {"DD_TAGS", "version:abcd"}}, {}},
  }));

  // Setup
  for (const auto &env_var : test_case.environment_variables) {
    REQUIRE(setenv(env_var.first.c_str(), env_var.second.c_str(), 1) == 0);
  }

  auto got = applyTracerOptionsFromEnvironment(input);
  REQUIRE(got);
  REQUIRE(test_case.tags == got->tags);

  // Teardown
  for (const auto &env_var : test_case.environment_variables) {
    REQUIRE(unsetenv(env_var.first.c_str()) == 0);
  }
}
