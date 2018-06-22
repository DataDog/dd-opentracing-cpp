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
  double sample_rate = 1.0;
  // Max amount of time to wait between sending traces to agent, in ms. Agent discards traces older
  // than 10s, so that is the upper bound.
  int64_t write_period_ms = 1000;
};

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_INCLUDE_OPENTRACING_TRACER_H
