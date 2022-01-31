#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sample.h"
#include "span.h"

namespace datadog {
namespace opentracing {

class Writer;
class SpanContext;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

// `PendingTrace` is an implementation detail of `SpanBuffer`.  A
// `PendingTrace` contains all of the information associated with a trace as it
// is happening.  When all of the spans in a `PendingTrace` have finished,
// `SpanBuffer` finalizes the spans and writes them (e.g. to the agent)
// together as a trace.
struct PendingTrace {
  PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id)
      : logger(logger),
        trace_id(trace_id),
        finished_spans(Trace{new std::vector<std::unique_ptr<SpanData>>()}),
        all_spans() {}
  // This constructor is only used in propagation tests.
  PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id,
               std::unique_ptr<SamplingPriority> sampling_priority)
      : logger(logger),
        trace_id(trace_id),
        finished_spans(Trace{new std::vector<std::unique_ptr<SpanData>>()}),
        all_spans(),
        sampling_priority(std::move(sampling_priority)) {}

  void finish();
  // TODO: document
  // Note that this function is idempotent.
  void applySamplingDecisionToUpstreamServices();

  std::shared_ptr<const Logger> logger;
  uint64_t trace_id;
  Trace finished_spans;
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
  // trace tags require special handling, e.g. "_dd.p.upstream_services".
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
  // `applied_sampling_decision_to_upstream_services` is whether the function
  // `applySamplingDecisionToUpstreamServices` has done its work.
  bool applied_sampling_decision_to_upstream_services = false;
};

struct SpanBufferOptions {
  bool enabled = true;
  std::string hostname;
  double analytics_rate = std::nan("");
  std::string service;
  // See the corresponding field in `TracerOptions`.
  uint64_t trace_tags_propagation_max_length;
};

// Keeps track of Spans until there is a complete trace, and sends completed
// traces to a Writer.
class SpanBuffer {
 public:
  SpanBuffer(std::shared_ptr<const Logger> logger, std::shared_ptr<Writer> writer,
             std::shared_ptr<RulesSampler> sampler, SpanBufferOptions options);
  virtual ~SpanBuffer() = default;

  void registerSpan(const SpanContext& context);
  void finishSpan(std::unique_ptr<SpanData> span);

  OptionalSamplingPriority getSamplingPriority(uint64_t trace_id) const;

  // The following documentation applies to all of the functions
  // `setSamplingPriorityFrom[...]`.
  //
  // If the sampling priority has not yet been set for the trace having the
  // specified `trace_id`, set that trace's sampling priority to a value
  // derived from the specified `value`.  Return a copy of the trace's
  // resulting sampling priority, which might or not have been altered by this
  // operation.
  //
  // The name of the function indicates in which context it is to be called.
  // For example, `setSamplingPriorityFromUser` is called to set the
  // sampling priority when it is decided by a method invoked on a `Span` by
  // client code.
  OptionalSamplingPriority setSamplingPriorityFromUser(
      uint64_t trace_id, const std::unique_ptr<UserSamplingPriority>& value);
  // There is also the private `setSamplingPriorityFromSampler` and
  // `setSamplingPriorityFromExtractedContext`.

  // Make a sampling decision for the trace corresponding to the specified
  // `span` if a sampling decision has not already been made. Return the
  // resulting sampling decision.
  OptionalSamplingPriority generateSamplingPriority(const SpanData* span);

  // Return the serialization of the trace tags associated with the trace
  // having the specified `trace_id`, or return an empty string if an error
  // occurs.  Note that an empty string could mean either that there no tags or
  // that an error occurred.  If an encoding error occurs, a corresponding
  // `_dd.propagation_error` tag value will be added to the relevant trace's
  // local root span.
  std::string serializeTraceTags(uint64_t trace_id);

  // Change the name of the service associated with the trace having the
  // specified `trace_id` to the specified `service_name`.
  void setServiceName(uint64_t trace_id, ot::string_view service_name);

  // Do not permit any further changes to the sampling decision for the trace
  // having the specified `trace_id`.
  void lockSamplingPriority(uint64_t trace_id);

  // Causes the Writer to flush, but does not send any PendingTraces.
  // This function is `virtual` so that it can be overridden in unit tests.
  virtual void flush(std::chrono::milliseconds timeout);

 private:
  // Each method whose name ends with "Impl" is a non-mutex-locking version of
  // the corresponding method without the "Impl".

  OptionalSamplingPriority getSamplingPriorityImpl(uint64_t trace_id) const;

  OptionalSamplingPriority setSamplingPriorityFromUserImpl(
      uint64_t trace_id, const std::unique_ptr<UserSamplingPriority>& value);
  // `setSamplingPriorityFromSampler` and
  // `setSamplingPriorityFromExtractedContext` are called internally, so they
  // don't need mutex-locking versions.
  OptionalSamplingPriority setSamplingPriorityFromSampler(uint64_t trace_id,
                                                          const SampleResult& value);
  OptionalSamplingPriority setSamplingPriorityFromExtractedContext(uint64_t trace_id,
                                                                   SamplingPriority value);

  OptionalSamplingPriority generateSamplingPriorityImpl(const SpanData* span);

  void setSamplerResult(uint64_t trace_id, const SampleResult& sample_result);

  void lockSamplingPriorityImpl(uint64_t trace_id);

  std::shared_ptr<const Logger> logger_;
  std::shared_ptr<Writer> writer_;
  mutable std::mutex mutex_;
  std::shared_ptr<RulesSampler> sampler_;

 protected:
  // Exists to make it easy for a subclass (ie, our testing mock) to override on-trace-finish
  // behaviour.
  virtual void unbufferAndWriteTrace(uint64_t trace_id);

  std::unordered_map<uint64_t, PendingTrace> traces_;
  SpanBufferOptions options_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
