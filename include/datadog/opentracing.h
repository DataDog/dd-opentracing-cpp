#ifndef DD_INCLUDE_OPENTRACING_TRACER_H
#define DD_INCLUDE_OPENTRACING_TRACER_H

#include <opentracing/tracer.h>

#include <map>
#include <set>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// The type of headers that are used for propagating distributed traces.
// B3 headers only support 64 bit trace IDs.
enum class PropagationStyle {
  // Using Datadog headers.
  Datadog,
  // Use B3 headers.
  // https://github.com/openzipkin/b3-propagation
  B3,
};

struct TracerOptions {
  // Hostname or IP address of the Datadog agent. Can also be set by the environment variable
  // DD_AGENT_HOST.
  std::string agent_host = "localhost";
  // Port on which the Datadog agent is running. Can also be set by the environment variable
  // DD_TRACE_AGENT_PORT
  uint32_t agent_port = 8126;
  // The name of the service being traced.
  // See:
  // https://help.datadoghq.com/hc/en-us/articles/115000702546-What-is-the-Difference-Between-Type-Service-Resource-and-Name-
  std::string service;
  // The type of service being traced.
  // (see above URL for definition)
  std::string type = "web";
  // The environment this trace belongs to. eg. "" (env:none), "staging", "prod". Can also be set
  // by the environment variable DD_ENV
  std::string environment = "";
  // Client side sampling. The percentage of traces are sent to the agent, real number in [0, 1].
  // 0 = discard all traces, 1 = keep all traces.
  // Setting this lower reduces performance overhead at the cost of less data.
  double sample_rate = 1.0;
  // If true, disables client-side sampling (thus ignoring sample_rate) and enables distributed
  // priority sampling, where traces are sampled based on a combination of user-assigned priorities
  // and configuration from the agent.
  bool priority_sampling = true;
  // Max amount of time to wait between sending traces to agent, in ms. Agent discards traces older
  // than 10s, so that is the upper bound.
  int64_t write_period_ms = 1000;
  // If not empty, the given string overrides the operation name (and the overridden operation name
  // is recorded in the tag "operation").
  std::string operation_name_override = "";
  // The style of propagation headers to accept/extract. Can also be set by the environment
  // variable DD_PROPAGATION_STYLE_EXTRACT.
  std::set<PropagationStyle> extract{PropagationStyle::Datadog};
  // The style of propagation headers to emit/inject. Can also be set by the environment variable
  // DD_PROPAGATION_STYLE_INJECT.
  std::set<PropagationStyle> inject{PropagationStyle::Datadog};
};

// TraceEncoder exposes the data required to encode and submit traces to the
// Datadog Agent.
class TraceEncoder {
 public:
  TraceEncoder() {}
  virtual ~TraceEncoder() {}

  // Returns the Datadog Agent endpoint that traces should be sent to.
  virtual const std::string& path() = 0;
  virtual std::size_t pendingTraces() = 0;
  virtual void clearTraces() = 0;
  // Returns the HTTP headers that are required for the collection of traces.
  virtual const std::map<std::string, std::string> headers() = 0;
  // Returns the encoded payload from the collection of traces.
  virtual const std::string payload() = 0;
  // Receives and handles the response from the Agent.
  virtual void handleResponse(const std::string& response) = 0;
};

// makeTracer returns an opentracing::Tracer that submits traces to the Datadog Agent.
// This should be used when control over the HTTP requests to the Datadog Agent is not required.
std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions& options);
// makeTracerAndEncoder initializes an opentracing::Tracer and provides an encoder
// to use when submitting traces to the Datadog Agent.
// This should be used in applications that need to also control the HTTP requests to the Datadog
// Agent. eg. Envoy
std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>> makeTracerAndEncoder(
    const TracerOptions& options);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_INCLUDE_OPENTRACING_TRACER_H
