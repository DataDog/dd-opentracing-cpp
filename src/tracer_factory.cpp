#include "tracer_factory.h"
#include "string_view.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include "agent_writer.h"
#include "tracer.h"
#include "tracer_options.h"

using json = nlohmann::json;

namespace datadog {
namespace opentracing {
namespace {

// Search for the specified `key` in the specified JSON `object`.  If `object`
// has a value at `key`, then invoke the specified `handle_value` callback with
// the found value as an argument.
template <typename Callback>
void for_key(const json& object, const ot::string_view key, Callback&& handle_value) {
  const auto found = object.find(key);
  if (found != object.end()) {
    handle_value(*found);
  }
}

}  // namespace

ot::expected<TracerOptions> optionsFromConfig(const char* configuration,
                                              std::string& error_message) {
  TracerOptions options{"localhost", 8126, "", "web", "", 1.0};
  json config;
  try {
    config = json::parse(configuration);
  } catch (const json::parse_error&) {
    error_message = "configuration is not valid JSON";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  // `parse_option` is a shorthand for assigning an option based on a config
  // key, if present.
  const auto parse_option = [&config](const ot::string_view key, auto& destination) {
    for_key(config, key, [&](const auto& value) { value.get_to(destination); });
  };
  try {
    // Mandatory config, but may be set via environment.
    parse_option("service", options.service);
    // Optional.
    parse_option("agent_host", options.agent_host);
    parse_option("agent_port", options.agent_port);
    parse_option("agent_url", options.agent_url);
    parse_option("type", options.type);
    parse_option("environment", options.environment);
    parse_option("tags", options.tags);
    parse_option("version", options.version);
    parse_option("sample_rate", options.sample_rate);
    for_key(config, "sampling_rules",
            [&](const json& value) { options.sampling_rules = value.dump(); });
    parse_option("operation_name_override", options.operation_name_override);
    for_key(config, "propagation_style_extract", [&](const json& value) {
      const auto styles = asPropagationStyle(value.get<std::vector<std::string>>());
      if (!styles || styles.value().size() == 0) {
        error_message =
            "Invalid value for propagation_style_extract, must be a list of at least one element "
            "with value 'Datadog', or 'B3'";
        throw styles.get_unexpected();
      }
      options.extract = styles.value();
    });
    for_key(config, "propagation_style_inject", [&](const json& value) {
      auto styles = asPropagationStyle(value.get<std::vector<std::string>>());
      if (!styles || styles.value().size() == 0) {
        error_message =
            "Invalid value for propagation_style_inject, must be a list of at least one element "
            "with value 'Datadog', or 'B3'";
        throw styles.get_unexpected();
      }
      options.inject = styles.value();
    });
    parse_option("dd.trace.report-hostname", options.report_hostname);
    parse_option("dd.trace.analytics-enabled", options.analytics_enabled);
    parse_option("dd.trace.analytics-sample-rate", options.analytics_rate);
  } catch (const nlohmann::detail::type_error& error) {
    error_message = "configuration has an argument with an incorrect type: ";
    error_message += error.what();
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  } catch (const ot::unexpected_type<std::error_code>& error) {
    return error;
  }

  auto maybe_options = applyTracerOptionsFromEnvironment(options);
  if (!maybe_options) {
    error_message = maybe_options.error();
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  options = maybe_options.value();
  // sanity-check for final option values
  if (options.service.empty()) {
    error_message =
        "tracer option 'service' has not been set via config or DD_SERVICE environment variable";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  return options;
}

// Accepts configuration in JSON format, with the following keys:
// "service": Required. A string, the name of the service.
// "agent_host": A string, defaults to localhost. Can also be set by the environment variable
//     DD_AGENT_HOST
// "agent_port": A number, defaults to 8126. "type": A string, defaults to web. Can also be set by
//     the environment variable DD_TRACE_AGENT_PORT
// "type": A string, defaults to web.
// "environment": A string, defaults to "". The environment this trace belongs to.
//     eg. "" (env:none), "staging", "prod". Can also be set by the environment variable
//     DD_ENV
// "sample_rate": A double, defaults to 1.0.
// "operation_name_override": A string, if not empty it overrides the operation name (and the
//     overridden operation name is recorded in the tag "operation").
// "propagation_style_extract": A list of strings, each string is one of "Datadog", "B3". Defaults
//     to ["Datadog"]. The type of headers to use to propagate distributed traces. Can also be set
//     by the environment variable DD_PROPAGATION_STYLE_EXTRACT.
// "propagation_style_inject": A list of strings, each string is one of "Datadog", "B3". Defaults
//     to ["Datadog"]. The type of headers to use to receive distributed traces. Can also be set by
//     the environment variable DD_PROPAGATION_STYLE_INJECT.
//
// Extra keys will be ignored.
template <class TracerImpl>
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory<TracerImpl>::MakeTracer(
    const char* configuration, std::string& error_message) const noexcept try {
  auto maybe_options = optionsFromConfig(configuration, error_message);
  if (!maybe_options) {
    return ot::make_unexpected(maybe_options.error());
  }
  TracerOptions options = maybe_options.value();

  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(options.agent_host, options.agent_port, options.agent_url,
                      std::chrono::milliseconds(llabs(options.write_period_ms)), sampler)};

  return std::shared_ptr<ot::Tracer>{new TracerImpl{options, writer, sampler}};
} catch (const std::bad_alloc&) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

// Make sure we generate code for a Tracer-producing factory.
template class TracerFactory<Tracer>;

}  // namespace opentracing
}  // namespace datadog
