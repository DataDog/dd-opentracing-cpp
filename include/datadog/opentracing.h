#ifndef DD_INCLUDE_OPENTRACING_TRACER_H
#define DD_INCLUDE_OPENTRACING_TRACER_H

#ifdef _MSC_VER
#ifndef DD_OPENTRACING_STATIC
// dllexport/dllimport declspecs need to be applied when building a DLL
#ifdef DD_OPENTRACING_SHARED
#define DD_OPENTRACING_API __declspec(dllexport)
#else  // DD_OPENTRACING_SHARED
#define DD_OPENTRACING_API __declspec(dllimport)
#endif  // DD_OPENTRACING_SHARED
#else   // DD_OPENTRACING_STATIC
#define DD_OPENTRACING_API
#endif  // DD_OPENTRACING_STATIC
#else   // _MSC_VER
#define DD_OPENTRACING_API
#endif  // _MSC_VER

#include <opentracing/tracer.h>

#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Log levels used within the datadog tracer. The numeric values are arbitrary,
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
  // `sample_rate` is the default sampling rate for any trace unmatched by a
  // sampling rule.  Setting `sample_rate` is equivalent to appending to
  // `sampling_rules` a rule whose "sample_rate" is `sample_rate`.  If
  // `sample_rate` is NaN, then no default rule is added, and traces not
  // matching any sampling rule are subject to "priority sampling," where the
  // sampling rate is determined by the Datadog trace agent.  This option is
  // also configurable as the environment variable DD_TRACE_SAMPLE_RATE.
  double sample_rate = std::nan("");
  // This option is deprecated, and may be removed in future releases.
  bool priority_sampling = true;
  // Rule-based trace sampling is applied when initiating traces to determine
  // the sampling rate.  Configuration is specified as a JSON array of objects.
  // Each object must have a "sample_rate", while the "name" and "service"
  // fields are optional. The "sample_rate" value must be between 0.0 and 1.0
  // (inclusive).  Rules are checked in order, so a more specific rule should
  // be specified before a less specific rule.  Note that if the `sample_rate`
  // field of this `TracerOptions` has a non-NaN value, then there is an
  // implicit rule at the end of the list that matches any trace unmatched by
  // other rules, and applies a sampling rate of `sample_rate`.  If no rule
  // matches a trace, then "priority sampling" is applied instead, where the
  // sample rate is determined by the Datadog trace agent.  If any rules are
  // invalid, they are ignored. This option is also configurable as the
  // environment variable DD_TRACE_SAMPLING_RULES.
  std::string sampling_rules = "[]";
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
    std::string level_str;
    switch (level) {
      case LogLevel::debug:
        level_str = "debug";
        break;
      case LogLevel::info:
        level_str = "info";
        break;
      case LogLevel::error:
        level_str = "error";
        break;
      default:
        level_str = "<unknown level>";
        break;
    }
    std::cerr << level_str + ": " + message.data() + "\n";
  };
  // `sampling_limit_per_second` is the limit on the number of rule-controlled
  // traces that may be sampled per second.  This includes traces that match
  // the implicit "catch-all" rule appended to `sampling_rules`.  This option
  // is also configurable as the environment variable DD_TRACE_RATE_LIMIT.
  double sampling_limit_per_second = 100;
  // Some tags are associated with an entire trace, rather than with a
  // particular span in the trace.  Some of these trace-wide tags are
  // propagated between services.  The tags are injected into a carrier (e.g.
  // an HTTP header) in a particular format.
  // `tags_header_size` is the maximum length of the
  // serialized tags allowed.  Trace-wide tags whose serialized length exceeds
  // this limit are not propagated.
  uint64_t tags_header_size = 512;
  // Note about `span_sampling_rules`: Span sampling requires version 7.40 of
  // the Datadog Agent or a more recent version.
  //
  // Rule-based span sampling, which is distinct from rule-based trace
  // sampling, is used to determine which spans to keep, if any, when trace
  // sampling decides to drop the trace.
  // When the trace is to be dropped, each span is matched against the
  // `span_sampling_rules`.  For each span, the first rule to match, if any,
  // applies to the span and a span-specific sampling decision is made.  If the
  // decision for the span is to keep, then the span is sent to Datadog even
  // though the enclosing trace is not.
  // `span_sampling_rules` is a JSON array of objects, where each object has
  // the following shape:
  //
  //     {
  //       "service": <pattern>,
  //       "name": <pattern>,
  //       "sample_rate": <number between 0.0 and 1.0>,
  //       "max_per_second": <positive number>
  //     }
  //
  // The properties mean the following:
  //
  // - "service" is a glob pattern that must match a span's service name in
  //   order for the rule to match.  If "service" is not specified, then its
  //   default value is "*".  Glob patterns are described below.
  // - "name" is a glob pattern that must match a span's operation name in
  //   order for the rule to match.  If "name" is not specified, then its default
  //   value is "*".  Glob patterns are described below.
  // - "sample_rate" is the probability that a span matching the rule will be
  //   kept.  If "sample_rate" is not specified, then its default value is 1.0.
  // - "max_per_second" is the maximum number of spans that will be kept on
  //   account of this rule each second.  Spans that would cause the limit to
  //   be exceeded are dropped.  If "max_per_second" is not specified, then
  //   there is no limit.
  //
  // Glob patterns are a simplified form of regular expressions.  Certain
  // characters in a glob pattern have special meaning:
  //
  // - "*" matches any substring, including the empty string.
  // - "?" matches exactly one instance of any character.
  // - Other characters match exactly one instance of themselves.
  //
  // For example:
  //
  // - The glob pattern "foobar" is matched by "foobar" only.
  // - The glob pattern "foo*" is matched by "foobar", "foo", and "fooop", but
  //   not by "fond".
  // - The glob pattern "a?b*e*" is matched by "amble" and "albedo", but not by
  //   "albino".
  std::string span_sampling_rules = "[]";
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
DD_OPENTRACING_API std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions& options);
// makeTracerAndEncoder initializes an opentracing::Tracer and provides an encoder
// to use when submitting traces to the Datadog Agent.
// This should be used in applications that need to also control the HTTP requests to the Datadog
// Agent. eg. Envoy
std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>> makeTracerAndEncoder(
    const TracerOptions& options);

// Return a JSON representation of the specified `options`.  If the specified
// `with_timestamp` is `true`, then include a "date" field whose value is the
// current date and time.
// This function is defined in `tracer_options.cpp`.
std::string toJSON(const TracerOptions& options, bool with_timestamp);

// Return a reference to the options used to configure the specified `tracer`.
// The behavior is undefined unless `tracer` is a Datadog tracer.
// This function is defined in `tracer.cpp`.
const TracerOptions& getOptions(const ot::Tracer& tracer);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_INCLUDE_OPENTRACING_TRACER_H
