#include "span_buffer.h"
#include <iostream>
#include "span.h"
#include "writer.h"

namespace datadog {
namespace opentracing {

namespace {
const std::string sampling_priority_metric = "_sampling_priority_v1";
}  // namespace

void PendingTrace::finish(const std::shared_ptr<SampleProvider>& sampler) {
  if (finished_spans->size() == 0) {
    return;  // I don't know why this would ever happen.
  }
  // Check for sampling.
  SpanData* any_span = finished_spans->at(0).get();
  OptionalSamplingPriority sampling_priority =
      root_context->assignSamplingPriority(sampler, any_span);
  if (sampling_priority != nullptr) {
    // Set the metric for every span in the trace.
    for (auto& span : *finished_spans) {
      span->metrics[sampling_priority_metric] = static_cast<int>(*sampling_priority);
    }
  }
}

WritingSpanBuffer::WritingSpanBuffer(std::shared_ptr<Writer> writer) : writer_(writer) {}

void WritingSpanBuffer::registerSpan(const std::shared_ptr<SpanContext>& context) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = context->traceId();
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    traces_.emplace(std::make_pair(trace_id, PendingTrace{}));
    trace = traces_.find(trace_id);
    trace->second.root_context = context;
  }
  trace->second.all_spans.insert(context->id());
}

void WritingSpanBuffer::finishSpan(std::unique_ptr<SpanData> span,
                                   const std::shared_ptr<SampleProvider>& sampler) {
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
    // Entire trace is finished!
    trace.finish(sampler);
    writer_->write(std::move(trace.finished_spans));
    traces_.erase(trace_iter);
  }
}

std::shared_ptr<SpanContext> WritingSpanBuffer::getRootSpanContext(uint64_t trace_id) const {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    return nullptr;
  }
  return trace->second.root_context;
}

}  // namespace opentracing
}  // namespace datadog
