#include <datadog/opentracing.h>
#include "agent_writer.h"
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(options.agent_host, options.agent_port,
                      std::chrono::milliseconds(llabs(options.write_period_ms)))};
  return std::shared_ptr<ot::Tracer>{new Tracer{options, writer}};
}

}  // namespace opentracing
}  // namespace datadog
