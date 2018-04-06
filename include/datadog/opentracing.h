#include <opentracing/tracer.h>

namespace datadog {

struct TracerOptions {
  std::string agent_host = "localhost";
  uint32_t agent_port = 8126;
  std::string service_name = "nginx";
};

std::shared_ptr<opentracing::Tracer> makeTracer(const TracerOptions &options);

}  // namespace datadog