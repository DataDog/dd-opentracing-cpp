#include "tracer_factory.h"

#include <iostream>
#include <nlohmann/json.hpp>

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
    // Mandatory config, but may be set via environment.
    if (config.find("service") != config.end()) {
      config.at("service").get_to(options.service);
    }
    // Optional.
    if (config.find("agent_host") != config.end()) {
      config.at("agent_host").get_to(options.agent_host);
    }
    if (config.find("agent_port") != config.end()) {
      config.at("agent_port").get_to(options.agent_port);
    }
    if (config.find("agent_url") != config.end()) {
      config.at("agent_url").get_to(options.agent_url);
    }
    if (config.find("type") != config.end()) {
      config.at("type").get_to(options.type);
    }
    if (config.find("environment") != config.end()) {
      config.at("environment").get_to(options.environment);
    }
    if (config.find("tags") != config.end()) {
      config.at("tags").get_to(options.tags);
    }
    if (config.find("version") != config.end()) {
      config.at("version").get_to(options.version);
    }
    if (config.find("sample_rate") != config.end()) {
      config.at("sample_rate").get_to(options.sample_rate);
    }
    if (config.find("sampling_rules") != config.end()) {
      options.sampling_rules = config.at("sampling_rules").dump();
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
    if (config.find("dd.trace.report-hostname") != config.end()) {
      config.at("dd.trace.report-hostname").get_to(options.report_hostname);
    }
    if (config.find("dd.trace.analytics-enabled") != config.end()) {
      config.at("dd.trace.analytics-enabled").get_to(options.analytics_enabled);
    }
    if (config.find("dd.trace.analytics-sample-rate") != config.end()) {
      config.at("dd.trace.analytics-sample-rate").get_to(options.analytics_rate);
    }
    if (config.find("sampling_limit_per_second") != config.end()) {
      config.at("sampling_limit_per_second").get_to(options.sampling_limit_per_second);
    }
    if (config.find("sample_rate") != config.end()) {
      config.at("sample_rate").get_to(options.sample_rate);
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

  options = maybe_options.value();
  // sanity-check for final option values
  if (options.service.empty()) {
    error_message =
        "tracer option 'service' has not been set via config or DD_SERVICE environment variable";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  return options;
}

}  // namespace opentracing
}  // namespace datadog
