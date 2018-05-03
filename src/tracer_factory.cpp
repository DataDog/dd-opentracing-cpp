#include "tracer_factory.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include "tracer.h"

using json = nlohmann::json;

namespace datadog {
namespace opentracing {

// Accepts configuration in JSON format, with the following keys:
// "service": Required. A string, the name of the service.
// "span_name": Required. A string, the name of the spans.
// "agent_host": A string, defaults to localhost.
// "agent_port": A number, defaults to 8126.
// "type": A string, defaults to web.
// Extra keys will be ignored.
template <class TracerImpl>
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory<TracerImpl>::MakeTracer(
    const char *configuration, std::string &error_message) const noexcept try {
  TracerOptions options{"localhost", 8126, "", "", "web"};
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
    if (config.find("span_name") == config.end()) {
      error_message = "configuration argument 'span_name' is missing";
      return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    options.span_name = config["span_name"];
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
  } catch (const nlohmann::detail::type_error &) {
    error_message = "configuration has an argument with an incorrect type";
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  return std::shared_ptr<ot::Tracer>{new TracerImpl{options}};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

// Make sure we generate code for a Tracer-producing factory.
template class TracerFactory<Tracer>;

}  // namespace opentracing
}  // namespace datadog
