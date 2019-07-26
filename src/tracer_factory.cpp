#include "tracer_factory.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include "agent_writer.h"
#include "tracer.h"
#include "tracer_options.h"

using json = nlohmann::json;

namespace datadog {
namespace opentracing {

ot::expected<TracerOptions> optionsFromConfig(const char *configuration,
                                              std::string &error_message) {
  TracerOptions options{"localhost", 8126, "", "web", "", 1.0};
  json config;
  try {
    config = json::parse(configuration);
  } catch (const json::parse_error &) {
    error_message = "configuration is not valid JSON";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  try {
    // Mandatory config.
    if (config.find("service") == config.end()) {
      error_message = "configuration argument 'service' is missing";
      return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    config.at("service").get_to(options.service);
    // Optional.
    if (config.find("agent_host") != config.end()) {
      config.at("agent_host").get_to(options.agent_host);
    }
    if (config.find("agent_port") != config.end()) {
      config.at("agent_port").get_to(options.agent_port);
    }
    if (config.find("type") != config.end()) {
      config.at("type").get_to(options.type);
    }
    if (config.find("environment") != config.end()) {
      config.at("environment").get_to(options.environment);
    }
    if (config.find("sample_rate") != config.end()) {
      config.at("sample_rate").get_to(options.sample_rate);
    }
    if (config.find("dd.priority.sampling") != config.end()) {
      config.at("dd.priority.sampling").get_to(options.priority_sampling);
    }
    if (config.find("operation_name_override") != config.end()) {
      config.at("operation_name_override").get_to(options.operation_name_override);
    }
    if (config.find("propagation_style_extract") != config.end()) {
      auto styles = asPropagationStyle(
          config.at("propagation_style_extract").get<std::vector<std::string>>());
      if (!styles || styles.value().size() == 0) {
        error_message =
            "Invalid value for propagation_style_extract, must be a list of at least one element "
            "with value 'Datadog', or 'B3'";
        return styles.get_unexpected();
      }
      options.extract = styles.value();
    }
    if (config.find("propagation_style_inject") != config.end()) {
      auto styles = asPropagationStyle(
          config.at("propagation_style_inject").get<std::vector<std::string>>());
      if (!styles || styles.value().size() == 0) {
        error_message =
            "Invalid value for propagation_style_inject, must be a list of at least one element "
            "with value 'Datadog', or 'B3'";
        return styles.get_unexpected();
      }
      options.inject = styles.value();
    }
  } catch (const nlohmann::detail::type_error &) {
    error_message = "configuration has an argument with an incorrect type";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  auto maybe_options = applyTracerOptionsFromEnvironment(options);
  if (!maybe_options) {
    error_message = maybe_options.error();
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  return maybe_options.value();
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
// "dd.priority.sampling": A boolean, true by default. If true disables client-side sampling (thus
//     ignoring sample_rate) and enables distributed priority sampling, where traces are sampled
//     based on a combination of user-assigned priorities and configuration from the agent.
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
    const char *configuration, std::string &error_message) const noexcept try {
  auto maybe_options = optionsFromConfig(configuration, error_message);
  if (!maybe_options) {
    return ot::make_unexpected(maybe_options.error());
  }
  TracerOptions options = maybe_options.value();

  std::shared_ptr<SampleProvider> sampler = sampleProviderFromOptions(options);
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(options.agent_host, options.agent_port,
                      std::chrono::milliseconds(llabs(options.write_period_ms)), sampler)};

  return std::shared_ptr<ot::Tracer>{new TracerImpl{options, writer, sampler}};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

// Make sure we generate code for a Tracer-producing factory.
template class TracerFactory<Tracer>;

}  // namespace opentracing
}  // namespace datadog
