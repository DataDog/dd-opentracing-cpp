#include <opentracing/tracer.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

struct TracerOptions {
  std::string agent_host = "localhost";
  uint32_t agent_port = 8126;
  std::string service_name = "nginx";
};

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options);

}  // namespace opentracing
}  // namespace datadog