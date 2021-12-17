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

// `PendingTrace` is an implementation detail of `WritingSpanBuffer`.  A
// `PendingTrace` contains all of the information associated with a trace as it
// is happening.  When all of the spans in a `PendingTrace` have finished,
// `WritingSpanBuffer` finalizes the spans and writes them (e.g. to the agent)
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
  // If this service's sampling decision is the first in the trace, or if it
  // differs from the previous service's sampling decision, append an
  // `UpstreamService` value to `upstream_services` indicating this service's
  // sampling decision.  The behavior is undefined unless
  // `sampling_priority != nullptr`.  Note that because this function does not
  // append an `UpstreamService` if our sampling decision agrees with the
  // previous service's, this function is idempotent.
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
  // `trace_tags` are similarly added.  `trace_tags` names all begin with the
  // same prefix, `trace_tag_prefix`, defined in `tag_propagation.h`.
  // `trace_tags` originate from extracted trace context (`SpanContext`).  Some
  // trace tags require special handling, e.g. `upstream_services`, below.
  std::unordered_map<std::string, std::string> trace_tags;
  // `upstream_services` contains the parsed value of the
  // "_dd.p.upstream_services" tag from the trace tags.  Additionally, if this
  // service is the first to make a sampling decision, or if it changes the
  // sampling decision (currently not possible), `upstream_services` ends with
  // an `UpstreamService` object describing this service's sampling decision.
  // `upstream_services` is attached to the local root span as the
  // "_dd.p.upstream_services" tag.
  std::vector<UpstreamService> upstream_services;
  // `service` is the same service name specified in `TracerOptions` or deduced
  // from `DD_SERVICE`.
  std::string service;
  // If an error occurs while propagating trace tags (see
  // `WritingSpanBuffer::serializeTraceTags`), then the "_dd.propagation_error"
  // tag will be set on the local root span to the value of
  // `propagation_error`.  If no error occurs, then `propagation_error` will be
  // empty and the "_dd.propagation_error" tag will not be added.
  std::string propagation_error;
};

// Keeps track of Spans until there is a complete trace.
class SpanBuffer {
 public:
  SpanBuffer() {}
  virtual ~SpanBuffer() {}
  virtual void registerSpan(const SpanContext& context) = 0;
  virtual void finishSpan(std::unique_ptr<SpanData> span) = 0;
  virtual OptionalSamplingPriority getSamplingPriority(uint64_t trace_id) const = 0;
  // Set the sampling decision for the trace having specified `trace_id` to the
  // specified `priority` if a sampling decision has not already been made.
  // Return the resulting sampling decision.
  virtual OptionalSamplingPriority setSamplingPriority(uint64_t trace_id,
                                                       OptionalSamplingPriority priority) = 0;
  // Make a sampling decision for the trace corresponding to the specified
  // `span` if a sampling decision has not already been made. Return the
  // resulting sampling decision.
  virtual OptionalSamplingPriority assignSamplingPriority(const SpanData* span) = 0;
  // Return the serialization of the trace tags associated with the trace
  // having the specified `trace_id`, or return an empty string if an error
  // occurs.  Note that an empty string could mean either that there no tags or
  // that an error occurred.  If an encoding error occurs, a corresponding
  // `_dd.propagation_error` tag value will be added to the relevant trace's
  // local root span.
  virtual std::string serializeTraceTags(uint64_t trace_id) = 0;
  virtual void flush(std::chrono::milliseconds timeout) = 0;
};

struct WritingSpanBufferOptions {
  bool enabled = true;
  std::string hostname;
  double analytics_rate = std::nan("");
  std::string service;
  // See the corresponding field in `TracerOptions`.
  uint64_t trace_tags_propagation_max_length;
};

// A SpanBuffer that sends completed traces to a Writer.
class WritingSpanBuffer : public SpanBuffer {
 public:
  WritingSpanBuffer(std::shared_ptr<const Logger> logger, std::shared_ptr<Writer> writer,
                    std::shared_ptr<RulesSampler> sampler, WritingSpanBufferOptions options);

  void registerSpan(const SpanContext& context) override;
  void finishSpan(std::unique_ptr<SpanData> span) override;

  OptionalSamplingPriority getSamplingPriority(uint64_t trace_id) const override;
  OptionalSamplingPriority setSamplingPriority(uint64_t trace_id,
                                               OptionalSamplingPriority priority) override;
  OptionalSamplingPriority assignSamplingPriority(const SpanData* span) override;

  std::string serializeTraceTags(uint64_t trace_id) override;

  // Causes the Writer to flush, but does not send any PendingTraces.
  void flush(std::chrono::milliseconds timeout) override;

 private:
  // These xImpl methods exist so we can avoid using reentrant locks.
  OptionalSamplingPriority getSamplingPriorityImpl(uint64_t trace_id) const;
  OptionalSamplingPriority setSamplingPriorityImpl(uint64_t trace_id,
                                                   OptionalSamplingPriority priority);
  OptionalSamplingPriority assignSamplingPriorityImpl(const SpanData* span);
  void setSamplerResult(uint64_t trace_id, const SampleResult& sample_result);

  std::shared_ptr<const Logger> logger_;
  std::shared_ptr<Writer> writer_;
  mutable std::mutex mutex_;
  std::shared_ptr<RulesSampler> sampler_;

 protected:
  // Exists to make it easy for a subclass (ie, our testing mock) to override on-trace-finish
  // behaviour.
  virtual void unbufferAndWriteTrace(uint64_t trace_id);

  std::unordered_map<uint64_t, PendingTrace> traces_;
  WritingSpanBufferOptions options_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
