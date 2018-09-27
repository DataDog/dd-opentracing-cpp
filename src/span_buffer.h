#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace datadog {
namespace opentracing {

class Writer;
class SpanContext;
class SpanData;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

struct PendingTrace {
  PendingTrace()
      : finished_spans(Trace{new std::vector<std::unique_ptr<SpanData>>()}), all_spans() {}

  Trace finished_spans;
  std::unordered_set<uint64_t> all_spans;
  // The root span's context.
  std::shared_ptr<SpanContext> root_context;
};

// Keeps track of Spans until there is a complete trace.
class SpanBuffer {
 public:
  SpanBuffer() {}
  virtual ~SpanBuffer() {}
  virtual void registerSpan(const std::shared_ptr<SpanContext> context) = 0;
  virtual void finishSpan(std::unique_ptr<SpanData> span) = 0;
  virtual std::shared_ptr<SpanContext> getRootSpanContext(uint64_t trace_id) const = 0;
};

// A SpanBuffer that sends completed traces to a Writer.
class WritingSpanBuffer : public SpanBuffer {
 public:
  WritingSpanBuffer(std::shared_ptr<Writer> writer);

  void registerSpan(const std::shared_ptr<SpanContext> context) override;
  void finishSpan(std::unique_ptr<SpanData> span) override;
  std::shared_ptr<SpanContext> getRootSpanContext(uint64_t trace_id) const override;

 private:
  std::shared_ptr<Writer> writer_;
  std::unordered_map<uint64_t, PendingTrace> traces_;
  mutable std::mutex mutex_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
