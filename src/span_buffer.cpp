#include "span_buffer.h"

#include <iostream>

namespace datadog {
namespace opentracing {

WritingSpanBuffer::WritingSpanBuffer(std::shared_ptr<Writer> writer) : writer_(writer) {}

void WritingSpanBuffer::registerSpan(const SpanData& span) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = span.traceId();
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    traces_.emplace(std::make_pair(trace_id, PendingTrace{}));
    trace = traces_.find(trace_id);
  }
  trace->second.all_spans.insert(span.spanId());
}

void WritingSpanBuffer::finishSpan(std::unique_ptr<SpanData> span) {
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

}  // namespace opentracing
}  // namespace datadog
