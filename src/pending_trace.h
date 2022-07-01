#ifndef DD_OPENTRACING_PENDING_TRACE_H
#define DD_OPENTRACING_PENDING_TRACE_H

#include <memory>
#include <unordered_set>

#include "sample.h"
#include "sampling_priority.h"
#include "trace_data.h"

namespace datadog {
namespace opentracing {

class Logger;

// `PendingTrace` is an implementation detail of `SpanBuffer`.  A
// `PendingTrace` contains all of the information associated with a trace as it
// is happening.  When all of the spans in a `PendingTrace` have finished,
// `SpanBuffer` finalizes the spans and writes them (e.g. to the agent)
// together as a trace.
struct PendingTrace {
  PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id);

  // This constructor is only used in propagation tests.
  PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id,
               std::unique_ptr<SamplingPriority> sampling_priority);

  void finish();
  // If this tracer did not inherit a sampling decision from an upstream
  // service, but instead made a sampling decision, then record that decision
  // in the "_dd.p.dm" member of `trace_tags`.
  void applySamplingDecisionToTraceTags();

  std::shared_ptr<const Logger> logger;
  uint64_t trace_id;
  TraceData finished_spans;
  std::unordered_set<uint64_t> all_spans;
  OptionalSamplingPriority sampling_priority;
  bool sampling_priority_locked = false;
  std::string origin;
  std::string hostname;
  double analytics_rate;
  SampleResult sample_result;
  // `trace_tags` are tags that are associated with the entire local trace,
  // rather than with a single span.  Some other tags are added to the local
  // root span when the trace chunk is sent to the agent (see
  // `finish_root_span` in `span_buffer.cpp`).  In addition to those tags,
  // `trace_tags` are similarly added.
  // `trace_tags` originate from extracted trace context (`SpanContext`).  Some
  // trace tags require special handling, e.g. "_dd.p.dm".
  std::unordered_map<std::string, std::string> trace_tags;
  // `service` is the name of the service associated with this trace.  If the
  // service name changes (such as by calling `Span::setServiceName`), then
  // this is the most recent value.
  std::string service;
  // If an error occurs while propagating trace tags (see
  // `SpanBuffer::serializeTraceTags`), then the "_dd.propagation_error"
  // tag will be set on the local root span to the value of
  // `propagation_error`.  If no error occurs, then `propagation_error` will be
  // empty and the "_dd.propagation_error" tag will not be added.
  std::string propagation_error;
  // `sampling_decision_extracted` is whether `sampling_priority` was
  // determined by a decision within this tracer (`true`), or inherited from an
  // upstream service when span context was extracted (`false`), or has not yet
  // been decided (`false`).
  bool sampling_decision_extracted = false;
};

}  // namespace opentracing
}  // namespace datadog

#endif
