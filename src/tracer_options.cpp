#include "tracer_options.h"

#include <iterator>
#include <sstream>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

ot::expected<TracerOptions, const char *> applyTracerOptionsFromEnvironment(
    const TracerOptions &input) {
  TracerOptions opts = input;

  auto agent_host = std::getenv("DD_AGENT_HOST");
  if (agent_host != nullptr && std::strlen(agent_host) > 0) {
    opts.agent_host = agent_host;
  }

  auto environment = std::getenv("DD_ENV");
  if (environment != nullptr && std::strlen(environment) > 0) {
    opts.environment = environment;
  }

  auto trace_agent_port = std::getenv("DD_TRACE_AGENT_PORT");
  if (trace_agent_port != nullptr && std::strlen(trace_agent_port) > 0) {
    try {
      opts.agent_port = std::stoi(trace_agent_port);
    } catch (const std::invalid_argument &ia) {
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is invalid");
    } catch (const std::out_of_range &oor) {
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is out of range");
    }
  }

  auto extract = std::getenv("DD_PROPAGATION_STYLE_EXTRACT");
  if (extract != nullptr && std::strlen(extract) > 0) {
    std::stringstream words{extract};
    auto style_maybe = asPropagationStyle(std::vector<std::string>{
        std::istream_iterator<std::string>{words}, std::istream_iterator<std::string>{}});
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_EXTRACT is invalid");
    }
    opts.extract = style_maybe.value();
  }

  auto inject = std::getenv("DD_PROPAGATION_STYLE_INJECT");
  if (inject != nullptr && std::strlen(inject) > 0) {
    std::stringstream words{inject};
    auto style_maybe = asPropagationStyle(std::vector<std::string>{
        std::istream_iterator<std::string>{words}, std::istream_iterator<std::string>{}});
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_INJECT is invalid");
    }
    opts.inject = style_maybe.value();
  }

  return opts;
}

}  // namespace opentracing
}  // namespace datadog
