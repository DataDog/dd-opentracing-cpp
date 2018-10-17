#include "tracer_factory.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include "agent_writer.h"
#include "tracer.h"

using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {
ot::expected<PropagationStyle> asPropagationStyle(std::string s) {
  if (s == "DatadogOnly") {
    return PropagationStyle::DatadogOnly;
  } else if (s == "Both") {
    return PropagationStyle::Both;
  } else if (s == "B3Only") {
    return PropagationStyle::B3Only;
  }
  return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
}
}  // namespace

// Accepts configuration in JSON format, with the following keys:
// "service": Required. A string, the name of the service.
// "agent_host": A string, defaults to localhost.
// "agent_port": A number, defaults to 8126.
// "type": A string, defaults to web.
// "environment": A string, defaults to "". The environment this trace belongs to.
//     eg. "" (env:none), "staging", "prod"
// "sample_rate": A double, defaults to 1.0.
// "dd.priority.sampling": A boolean, false by default. If true disables client-side sampling (thus
//     ignoring sample_rate) and enables distributed priority sampling, where traces are sampled
//     based on a combination of user-assigned priorities and configuration from the agent.
// "operation_name_override": A string, if not empty it overrides the operation name (and the
//     overridden operation name is recorded in the tag "operation").
// "propagation_style_extract": A string, one of "DatadogOnly", "Both", "B3Only". Defaults to
//     "Both". The type of headers to use to propagate distributed traces.
// "propagation_style_inject": A string, one of "DatadogOnly", "Both", "B3Only". Defaults to
//     "DatadogOnly". The type of headers to use to receive distributed traces.
//
// Extra keys will be ignored.
template <class TracerImpl>
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory<TracerImpl>::MakeTracer(
    const char *configuration, std::string &error_message) const noexcept try {
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
    options.service = config["service"];
    // Optional.
    if (config.find("agent_host") != config.end()) {
      options.agent_host = config["agent_host"];
    }
    if (config.find("agent_port") != config.end()) {
      options.agent_port = config["agent_port"];
    }
    if (config.find("type") != config.end()) {
      options.type = config["type"];
    }
    if (config.find("environment") != config.end()) {
      options.environment = config["environment"];
    }
    if (config.find("sample_rate") != config.end()) {
      options.sample_rate = config["sample_rate"];
    }
    if (config.find("dd.priority.sampling") != config.end()) {
      options.priority_sampling = config["dd.priority.sampling"];
    }
    if (config.find("operation_name_override") != config.end()) {
      options.operation_name_override = config["operation_name_override"];
    }
    if (config.find("propagation_style_extract") != config.end()) {
      auto style = asPropagationStyle(config["propagation_style_extract"]);
      if (!style) {
        error_message =
            "Invalid value for propagation_style_extract, must be one of 'DatadogOnly', 'Both', "
            "'B3Only'";
        return style.get_unexpected();
      }
      options.extract = style.value();
    }
    if (config.find("propagation_style_inject") != config.end()) {
      auto style = asPropagationStyle(config["propagation_style_inject"]);
      if (!style) {
        error_message =
            "Invalid value for propagation_style_inject, must be one of 'DatadogOnly', 'Both', "
            "'B3Only'";
        return style.get_unexpected();
      }
      options.inject = style.value();
    }
  } catch (const nlohmann::detail::type_error &) {
    error_message = "configuration has an argument with an incorrect type";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
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
