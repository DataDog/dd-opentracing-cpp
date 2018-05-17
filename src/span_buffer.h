#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include "span.h"
#include "writer.h"

#include <mutex>
#include <unordered_map>

namespace datadog {
namespace opentracing {

class Span;
template <class Span>
class Writer;
template <class Span>
using Trace = std::unique_ptr<std::vector<Span>>;

template <class Span>
struct PendingTrace {
  PendingTrace() : finished_spans(std::make_unique<std::vector<Span>>()), all_spans() {}

  Trace<Span> finished_spans;
  std::unordered_set<uint64_t> all_spans;
};

// Keeps track of Spans until there is a complete trace.
template <class Span>
class SpanBuffer {
 public:
  SpanBuffer() {}
  virtual ~SpanBuffer() {}
  virtual void registerSpan(const Span& span) = 0;
  virtual void finishSpan(Span&& span) = 0;
};

// A SpanBuffer that sends completed traces to a Writer.
template <class Span>
class WritingSpanBuffer : public SpanBuffer<Span> {
 public:
  WritingSpanBuffer(std::shared_ptr<Writer<Span>> writer);

  void registerSpan(const Span& span) override;
  void finishSpan(Span&& span) override;

 private:
  std::shared_ptr<Writer<Span>> writer_;
  std::unordered_map<uint64_t, PendingTrace<Span>> traces_;
  mutable std::mutex mutex_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
