#include "tracer_factory.h"

#include <iostream>
#include "tracer.h"

namespace datadog {
namespace opentracing {

// We get something like this
// {"datadog_service_name":"envoy","datadog_agent_port":8126,"datadog_span_name":"fred","datadog_agent_hostname":"dd-agent-envoy"}
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory::MakeTracer(
    const char *configuration, std::string &error_message) const noexcept try {
  TracerOptions options{
      "dd-agent-envoy", 8126, "envoy", "route", "web",
  };
  std::cout << "_______________HERP DERP " << configuration << "\n\n\n";

  return std::shared_ptr<ot::Tracer>{new Tracer{options}};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
