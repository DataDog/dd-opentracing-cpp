#ifndef DD_INCLUDE_OPENTRACING_TRACER_H
#define DD_INCLUDE_OPENTRACING_TRACER_H

#include <opentracing/tracer.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

struct TracerOptions {
  std::string agent_host = "localhost";
  uint32_t agent_port = 8126;
  std::string service;
  std::string type = "web";
};

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_INCLUDE_OPENTRACING_TRACER_H
