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
// "agent_host": Defaults to localhost.
// "agent_port": Defaults to 8126.
// "type": Defaults to web.
// Extra keys will be ignored.
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory::MakeTracer(
    const char *configuration, std::string &error_message) const noexcept try {
  TracerOptions options{"localhost", 8126, "", "", "web"};
  json config = json::parse(configuration);
  // Mandatory config.
  if (config.find("service") == config.end()) {
    std::cerr << "Datadog configuration argument 'service' is missing" << std::endl;
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  options.service = config["service"];
  if (config.find("span_name") == config.end()) {
    std::cerr << "Datadog configuration argument 'span_name' is missing" << std::endl;
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  options.span_name = config["span_name"];
  // Optional.
  if (config.find("agent_host") != config.end()) {
    options.agent_host = config["agent_host"];
  }
  if (config.find("agent_port") != config.end()) {
    std::string port = config["agent_host"];
    options.agent_port = std::stoi(port);
  }
  if (config.find("type") != config.end()) {
    options.type = config["type"];
  }
  return std::shared_ptr<ot::Tracer>{new Tracer{options}};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
