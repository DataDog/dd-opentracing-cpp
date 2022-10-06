#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pending_trace.h"
#include "sample.h"
#include "span.h"
#include "trace_data.h"

namespace datadog {
namespace opentracing {

class Writer;
class SpanContext;
class SpanSampler;

struct SpanBufferOptions {
  bool enabled = true;
  std::string hostname;
  double analytics_rate = std::nan("");
  std::string service;
  // See the corresponding field in `TracerOptions`.
  uint64_t tags_header_size;
};

// Keeps track of Spans until there is a complete trace, and sends completed
// traces to a Writer.
class SpanBuffer {
 public:
  // Create a span buffer that:
  //
  // - uses the specified `logger` to log diagnostics,
  // - uses the specified `writer` to output completed trace segments,
  // - uses the specified `trace_sampler` to make decisions about whether to keep traces,
  // - uses the specified `span_sampler` to make decisions about whether to keep spans when a trace
  //   is dropped,
  // - is configured using the specified `options`.
  //
  // If `span_sampler` is `nullptr`, then span sampling is disabled (but
  // `trace_sampler` is still consulted for trace sampling decisions).
  SpanBuffer(std::shared_ptr<const Logger> logger, std::shared_ptr<Writer> writer,
             std::shared_ptr<RulesSampler> trace_sampler,
             std::shared_ptr<SpanSampler> span_sampler, SpanBufferOptions options);
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
  // having the specified `trace_id`, or return `nullptr` if an error occurs.
  // If an encoding error occurs, a corresponding `_dd.propagation_error` tag
  // value will be added to the relevant trace's local root span.
  std::unique_ptr<std::string> serializeTraceTags(uint64_t trace_id);

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
  std::shared_ptr<RulesSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;

 protected:
  // Exists to make it easy for a subclass (ie, our testing mock) to override on-trace-finish
  // behaviour.
  virtual void unbufferAndWriteTrace(
      std::unordered_map<uint64_t, PendingTrace>::iterator trace_iter);

  std::unordered_map<uint64_t, PendingTrace> traces_;
  SpanBufferOptions options_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
