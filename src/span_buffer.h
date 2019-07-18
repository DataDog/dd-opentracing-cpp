#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "span.h"

namespace datadog {
namespace opentracing {

class Writer;
class SpanContext;
class SampleProvider;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

struct PendingTrace {
  PendingTrace()
      : finished_spans(Trace{new std::vector<std::unique_ptr<SpanData>>()}), all_spans() {}

  void finish();

  Trace finished_spans;
  std::unordered_set<uint64_t> all_spans;
  OptionalSamplingPriority sampling_priority;
  bool sampling_priority_locked = false;
  std::string origin;
  std::string hostname;
  double analytics_rate;
};

// Keeps track of Spans until there is a complete trace.
class SpanBuffer {
 public:
  SpanBuffer() {}
  virtual ~SpanBuffer() {}
  virtual void registerSpan(const SpanContext& context) = 0;
  virtual void finishSpan(std::unique_ptr<SpanData> span,
                          const std::shared_ptr<SampleProvider>& sampler) = 0;
  virtual OptionalSamplingPriority getSamplingPriority(uint64_t trace_id) const = 0;
  virtual OptionalSamplingPriority setSamplingPriority(uint64_t trace_id,
                                                       OptionalSamplingPriority priority) = 0;
  virtual OptionalSamplingPriority assignSamplingPriority(
      const std::shared_ptr<SampleProvider>& sampler, const SpanData* span) = 0;
  virtual void flush(std::chrono::milliseconds timeout) = 0;
};

struct WritingSpanBufferOptions {
  std::string hostname;
  double analytics_rate = std::nan("");
};

// A SpanBuffer that sends completed traces to a Writer.
class WritingSpanBuffer : public SpanBuffer {
 public:
  WritingSpanBuffer(std::shared_ptr<Writer> writer, WritingSpanBufferOptions options);

  void registerSpan(const SpanContext& context) override;
  void finishSpan(std::unique_ptr<SpanData> span,
                  const std::shared_ptr<SampleProvider>& sampler) override;

  OptionalSamplingPriority getSamplingPriority(uint64_t trace_id) const override;
  OptionalSamplingPriority setSamplingPriority(uint64_t trace_id,
                                               OptionalSamplingPriority priority) override;
  OptionalSamplingPriority assignSamplingPriority(const std::shared_ptr<SampleProvider>& sampler,
                                                  const SpanData* span) override;

  // Causes the Writer to flush, but does not send any PendingTraces.
  void flush(std::chrono::milliseconds timeout) override;

 private:
  // These xImpl methods exist so we can avoid using reentrant locks.
  OptionalSamplingPriority getSamplingPriorityImpl(uint64_t trace_id) const;
  OptionalSamplingPriority setSamplingPriorityImpl(uint64_t trace_id,
                                                   OptionalSamplingPriority priority);
  OptionalSamplingPriority assignSamplingPriorityImpl(
      const std::shared_ptr<SampleProvider>& sampler, const SpanData* span);

  std::shared_ptr<Writer> writer_;
  mutable std::mutex mutex_;

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
