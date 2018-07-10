#include "span_buffer.h"

#include <iostream>

namespace datadog {
namespace opentracing {

template <class Span>
WritingSpanBuffer<Span>::WritingSpanBuffer(std::shared_ptr<Writer<Span>> writer)
    : writer_(writer) {}

template <class Span>
void WritingSpanBuffer<Span>::registerSpan(const Span& span) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = span.traceId();
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    traces_.emplace(std::make_pair(trace_id, PendingTrace<Span>{}));
    trace = traces_.find(trace_id);
  }
  trace->second.all_spans.insert(span.spanId());
}

template <class Span>
void WritingSpanBuffer<Span>::finishSpan(std::unique_ptr<Span> span) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  auto trace_iter = traces_.find(span->traceId());
  if (trace_iter == traces_.end()) {
    std::cerr << "Missing trace for finished span" << std::endl;
    return;
  }
  auto& trace = trace_iter->second;
  if (trace.all_spans.find(span->spanId()) == trace.all_spans.end()) {
    std::cerr << "A Span that was not registered was submitted to WritingSpanBuffer" << std::endl;
    return;
  }
  trace.finished_spans->push_back(std::move(span));
  if (trace.finished_spans->size() == trace.all_spans.size()) {
    writer_->write(std::move(trace.finished_spans));
    traces_.erase(trace_iter);
  }
}

// Make sure we generate code for a Span-buffering SpanBuffer.
template class WritingSpanBuffer<SpanData>;

}  // namespace opentracing
}  // namespace datadog
