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

struct Trace {
  std::unique_ptr<std::vector<Span>> finished_spans;
  size_t all_spans;
};

// Keeps track of Spans until there is a complete trace.
class SpanBuffer {
 public:
  SpanBuffer() {}
  virtual ~SpanBuffer() {}
  virtual void startSpan(uint64_t trace_id) = 0;
  virtual void finishSpan(Span&& span) = 0;
};

// A SpanBuffer that sends completed traces to a Writer.
class WritingSpanBuffer : public SpanBuffer {
 public:
  WritingSpanBuffer(std::shared_ptr<Writer<Span>> writer);

  void startSpan(uint64_t trace_id) override;
  void finishSpan(Span&& span) override;

 private:
  std::shared_ptr<Writer<Span>> writer_;
  std::unordered_map<uint64_t, Trace> traces_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
