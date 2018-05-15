#include "span_buffer.h"

#include <iostream>

namespace datadog {
namespace opentracing {

WritingSpanBuffer::WritingSpanBuffer(std::shared_ptr<Writer<Span>> writer) : writer_(writer) {}

void WritingSpanBuffer::startSpan(uint64_t trace_id) {
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    traces_.emplace(std::make_pair(trace_id, Trace{std::make_unique<std::vector<Span>>(), 0}));
    trace = traces_.find(trace_id);
  }
  trace->second.all_spans++;
}

void WritingSpanBuffer::finishSpan(Span&& span) {
  auto trace = traces_.find(span.traceId());
  if (trace == traces_.end()) {
    std::cerr << "Missing trace for finished span" << std::endl;
  }
  trace->second.finished_spans->emplace_back(std::move(span));
  if (trace->second.finished_spans->size() == trace->second.all_spans) {
    writer_->write(std::move(trace->second.finished_spans));
    traces_.erase(trace);
  }
}

}  // namespace opentracing
}  // namespace datadog
