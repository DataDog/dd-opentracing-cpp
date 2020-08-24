#ifndef DD_INCLUDE_OPENTRACING_TRACER_H
#define DD_INCLUDE_OPENTRACING_TRACER_H

#include <opentracing/tracer.h>

#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Log levels used within the datadog tracer. The numberic values are arbitrary,
// and the logging function is responsible for mapping these levels to the
// application-specific logger's levels.
enum class LogLevel {
  debug = 1,
  info = 2,
  error = 3,
};

using LogFunc = std::function<void(LogLevel, ot::string_view)>;

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
  // The name of the service being traced. Can also be set by the environment variable DD_SERVICE.
  // See:
  // https://help.datadoghq.com/hc/en-us/articles/115000702546-What-is-the-Difference-Between-Type-Service-Resource-and-Name-
  std::string service;
  // The type of service being traced.
  // (see above URL for definition)
  std::string type = "web";
  // The environment this trace belongs to. eg. "" (env:none), "staging", "prod". Can also be set
  // by the environment variable DD_ENV
  std::string environment = "";
  // This option is deprecated and may be removed in future releases.
  // It is equivalent to setting a sampling rule with only a "sample_rate".
  // Values must be between 0.0 and 1.0 (inclusive)
  double sample_rate = std::nan("");
  // This option is deprecated, and may be removed in future releases.
  bool priority_sampling = true;
  // Rules sampling is applied when initiating traces to determine the sampling rate.
  // Traces that do not match any rules fall back to using priority sampling, where the rate is
  // determined by a combination of user-assigned priorities and configuration from the agent.
  // Configuration is specified as a JSON array of objects. Each object must have a "sample_rate",
  // and the "name" and "service" fields are optional. The "sample_rate" value must be between 0.0
  // and 1.0 (inclusive). Rules are applied in configured order, so a specific match should be
  // specified before a wider match. If any rules are invalid, they are ignored. Can also be set by
  // the environment variable DD_TRACE_SAMPLING_RULES.
  std::string sampling_rules = R"([{"sample_rate": 1.0}])";
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
  // If true, injects the hostname into spans reported by this tracer. Can also be set by the
  // environment variable DD_TRACE_REPORT_HOSTNAME.
  bool report_hostname = false;
  // If true and global analytics rate is not set, spans will be tagged with an analytics rate
  // of 1.0. Can also be set by the environment variable DD_TRACE_ANALYTICS_ENABLED.
  bool analytics_enabled = false;
  // When set to a value between 0.0 and 1.0 (inclusive), spans will be tagged with the provided
  // value for analytics sampling rate. Can also be set by the environment variable
  // DD_TRACE_ANALYTICS_SAMPLE_RATE
  double analytics_rate = std::nan("");
  // Tags that are applied to all spans reported by this tracer. Can also be set by the environment
  // variable DD_TAGS.
  std::map<std::string, std::string> tags = {};
  // The version of the overall application being traced. Can also be set by the environment
  // variable DD_VERSION.
  std::string version = "";
  // The URL to use for submitting traces to the agent. If set, this will be used instead of
  // agent_host / agent_port. This URL supports http, https and unix address schemes.
  // If no scheme is set in the URL, a path to a UNIX domain socket is assumed.
  // Can also be set by the environment variable DD_TRACE_AGENT_URL.
  std::string agent_url = "";
  // A logging function that is called by the tracer when noteworthy events occur.
  // The default value uses std::cerr, and applications can inject their own logging function.
  LogFunc log_func = [](LogLevel level, ot::string_view message) {
    switch (level) {
      case LogLevel::debug:
        std::cerr << "debug: " << message << std::endl;
        break;
      case LogLevel::info:
        std::cerr << "info: " << message << std::endl;
        break;
      case LogLevel::error:
        std::cerr << "error: " << message << std::endl;
        break;
      default:
        std::cerr << "<unknown level>: " << message << std::endl;
        break;
    }
  };
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
