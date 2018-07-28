#ifndef DD_INCLUDE_OPENTRACING_TRACER_H
#define DD_INCLUDE_OPENTRACING_TRACER_H

#include <opentracing/tracer.h>

#include <deque>
#include <map>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

struct TracerOptions {
  // Hostname or IP address of the Datadog agent.
  std::string agent_host = "localhost";
  // Port on which the Datadog agent is running.
  uint32_t agent_port = 8126;
  // The name of the service being traced.
  // See:
  // https://help.datadoghq.com/hc/en-us/articles/115000702546-What-is-the-Difference-Between-Type-Service-Resource-and-Name-
  std::string service;
  // The type of service being traced.
  // (see above URL for definition)
  std::string type = "web";
  // What percentage of traces are sent to the agent, real number in [0, 1].
  // 0 = discard all traces, 1 = keep all traces.
  // Setting this lower reduces performance overhead at the cost of less data.
  double sample_rate = 1.0;
  // Max amount of time to wait between sending traces to agent, in ms. Agent discards traces older
  // than 10s, so that is the upper bound.
  int64_t write_period_ms = 1000;
  // If not empty, the given string overrides the operation name (and the overridden operation name
  // is recorded in the tag "operation").
  std::string operation_name_override = "";
};

// TracePublisher exposes the data required to publish traces to the
// Datadog Agent.
class TracePublisher {
 public:
  TracePublisher() {}
  virtual ~TracePublisher() {}

  // Returns the Datadog Agent endpoint that traces should be published to.
  virtual const std::string path() = 0;
  virtual std::size_t pendingTraces() = 0;
  virtual void clearTraces() = 0;
  virtual const std::map<std::string, std::string> headers() = 0;
  virtual const std::string payload() = 0;
};

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options);
std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options,
                                       std::shared_ptr<TracePublisher> &publisher);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_INCLUDE_OPENTRACING_TRACER_H
