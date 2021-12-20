#include "span_buffer.h"

#include <iostream>

#include "sample.h"
#include "span.h"
#include "writer.h"

namespace datadog {
namespace opentracing {

namespace {
const std::string sampling_priority_metric = "_sampling_priority_v1";
const std::string datadog_origin_tag = "_dd.origin";
const std::string datadog_hostname_tag = "_dd.hostname";
const std::string event_sample_rate_metric = "_dd1.sr.eausr";
const std::string rules_sampler_applied_rate = "_dd.rule_psr";
const std::string rules_sampler_limiter_rate = "_dd.limit_psr";
const std::string priority_sampler_applied_rate = "_dd.agent_psr";

#if 0
// Alter the specified `span` to prepare it for encoding with the specified
// `trace`.
void finish_span(const PendingTrace& trace, SpanData& span) {
  // Propagate the trace origin in every span, if present.  This allows, for
  // example, sampling to vary with the trace's stated origin.
  if (!trace.origin.empty()) {
    span.meta[datadog_origin_tag] = trace.origin;
  }
}

// Alter the specified toplevel (i.e. having no parent in the local trace) `span`
// to prepare it for encoding with the specified `trace`.
void finish_toplevel_span(const PendingTrace& trace, SpanData& span) {
  // Check for sampling.
  if (trace.sampling_priority != nullptr) {
    span.metrics[sampling_priority_metric] = static_cast<int>(*trace.sampling_priority);
    // The span's datadog origin tag is set in `finish_span`, below.
  }
  if (!trace.hostname.empty()) {
    span.meta[datadog_hostname_tag] = trace.hostname;
  }
  if (!std::isnan(trace.analytics_rate) &&
      span.metrics.find(event_sample_rate_metric) == span.metrics.end()) {
    span.metrics[event_sample_rate_metric] = trace.analytics_rate;
  }
  if (!std::isnan(trace.sample_result.rule_rate)) {
    span.metrics[rules_sampler_applied_rate] = trace.sample_result.rule_rate;
  }
  if (!std::isnan(trace.sample_result.limiter_rate)) {
    span.metrics[rules_sampler_limiter_rate] = trace.sample_result.limiter_rate;
  }
  if (!std::isnan(trace.sample_result.priority_rate)) {
    span.metrics[priority_sampler_applied_rate] = trace.sample_result.priority_rate;
  }
  // Forward to the finisher that applies to all spans (not just toplevel spans).
  finish_span(trace, span);
}
#endif

}  // namespace

void ActiveTrace::addSpan(uint64_t span_id) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  expected_spans_.insert(span_id);
}

void ActiveTrace::finishSpan(SpanContext& context, std::unique_ptr<SpanData> span_data) {
  // Make sure a sampling priority is set.
  if (context.topLevel() && !sampling_status_.is_set) {
    context.sample();
  }
  std::lock_guard<std::mutex> lock_guard{mutex_};
  // Finalize span details.
  if (context.topLevel()) {
    // Apply tags to toplevel spans when values are set.
    span_data->metrics[sampling_priority_metric] =
        static_cast<int>(sampling_status_.sample_result.sampling_priority);
    if (!hostname_.empty()) {
      span_data->meta[datadog_hostname_tag] = hostname_;
    }
    if (!std::isnan(analytics_rate_) &&
        span_data->metrics.find(event_sample_rate_metric) == span_data->metrics.end()) {
      span_data->metrics[event_sample_rate_metric] = analytics_rate_;
    }
    if (!std::isnan(sampling_status_.sample_result.rule_rate)) {
      span_data->metrics[rules_sampler_applied_rate] = sampling_status_.sample_result.rule_rate;
    }
    if (!std::isnan(sampling_status_.sample_result.limiter_rate)) {
      span_data->metrics[rules_sampler_limiter_rate] = sampling_status_.sample_result.limiter_rate;
    }
    if (!std::isnan(sampling_status_.sample_result.priority_rate)) {
      span_data->metrics[priority_sampler_applied_rate] =
          sampling_status_.sample_result.priority_rate;
    }
  }

  // Origin tag is applied to all spans.
  auto origin = context.origin();
  if (!origin.empty()) {
    span_data->meta[datadog_origin_tag] = origin;
  }

  // Store the span data.
  finished_spans_.emplace_back(std::move(span_data));
  // Submit the data to the writer if it's the final expected span.
  if (finished_spans_.size() == expected_spans_.size()) {
    if (writer_ != nullptr) {
      writer_->write(finished_spans_);
    }
    finished_spans_.clear();
    expected_spans_.clear();
  }
}

void ActiveTrace::setSamplingPriority(UserSamplingPriority priority) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  if (sampling_status_.is_propagated) {
    // TODO: log a warning, because changing the sampling priority isn't
    // allowed after it has been propagated
  }
  sampling_status_.is_set = true;
  sampling_status_.sample_result.sampling_priority = static_cast<SamplingPriority>(priority);
}

void ActiveTrace::setSampleResult(SampleResult result) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  if (sampling_status_.is_set) {
    // TODO: log a warning, since this should not be set yet.
    return;
  }
  sampling_status_.is_set = true;
  sampling_status_.sample_result = result;
}

void ActiveTrace::setPropagated() {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  if (!sampling_status_.is_set) {
    // TODO: log a warning, since this should be set before propagating
    return;
  }
  if (sampling_status_.is_propagated) {
    // TODO: log a warning, because this shouldn't be changed after
    // it was originally propagated
    return;
  }
  sampling_status_.is_propagated = true;
}

SamplingStatus ActiveTrace::samplingStatus() {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return sampling_status_;
}

#if 0


void PendingTrace::finish() {
  // Apply changes to spans, in particular treating the root / local-root
  // span as special.
  for (const auto& span : *finished_spans) {
    if (is_root(*span, all_spans)) {
      finish_root_span(*this, *span);
    } else {
      finish_span(*this, *span);
    }
  }
}

WritingSpanBuffer::WritingSpanBuffer(std::shared_ptr<const Logger> logger,
                                     std::shared_ptr<Writer> writer,
                                     std::shared_ptr<RulesSampler> sampler,
                                     WritingSpanBufferOptions options)
    : logger_(logger), writer_(writer), sampler_(sampler), options_(options) {}

void WritingSpanBuffer::registerSpan(const SpanContext& context) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = context.traceId();
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end() || trace->second.all_spans.empty()) {
    traces_.emplace(std::make_pair(trace_id, PendingTrace{logger_}));
    trace = traces_.find(trace_id);
    OptionalSamplingPriority p = context.getPropagatedSamplingPriority();
    trace->second.sampling_priority_propagated = p != nullptr;
    trace->second.sampling_priority = std::move(p);
    if (!context.origin().empty()) {
      trace->second.origin = context.origin();
    }
    trace->second.hostname = options_.hostname;
    trace->second.analytics_rate = options_.analytics_rate;
  }
  trace->second.all_spans.insert(context.id());
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
  uint64_t trace_id = span->traceId();
  trace.finished_spans->push_back(std::move(span));
  if (trace.finished_spans->size() == trace.all_spans.size()) {
    assignSamplingPriorityImpl(trace.finished_spans->back().get());
    trace.finish();
    unbufferAndWriteTrace(trace_id);
  }
}

void WritingSpanBuffer::unbufferAndWriteTrace(uint64_t trace_id) {
  auto trace_iter = traces_.find(trace_id);
  if (trace_iter == traces_.end()) {
    return;
  }
  auto& trace = trace_iter->second;
  if (options_.enabled) {
    writer_->write(std::move(trace.finished_spans));
  }
  traces_.erase(trace_iter);
}

void WritingSpanBuffer::flush(std::chrono::milliseconds timeout) { writer_->flush(timeout); }

OptionalSamplingPriority WritingSpanBuffer::getSamplingPriority(uint64_t trace_id) const {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return getSamplingPriorityImpl(trace_id);
}
OptionalSamplingPriority WritingSpanBuffer::getSamplingPriorityImpl(uint64_t trace_id) const {
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    logger_->Trace(trace_id, "cannot get sampling priority, trace not found");
    return nullptr;
  }
  if (trace->second.sampling_priority == nullptr) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(*trace->second.sampling_priority);
}

OptionalSamplingPriority WritingSpanBuffer::setSamplingPriority(
    uint64_t trace_id, OptionalSamplingPriority priority) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return setSamplingPriorityImpl(trace_id, std::move(priority));
}

OptionalSamplingPriority WritingSpanBuffer::setSamplingPriorityImpl(
    uint64_t trace_id, OptionalSamplingPriority priority) {
  auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot set sampling priority, trace not found");
    return nullptr;
  }
  PendingTrace& trace = trace_entry->second;
  if (trace.sampling_priority_propagated) {
    if (priority == nullptr || *priority == SamplingPriority::UserKeep ||
        *priority == SamplingPriority::UserDrop) {
      // Only print an error if a user is taking this action. This case is legitimate (albeit with
      // the same outcome) if the Sampler itself is trying to assignSamplingPriority.
      logger_->Trace(trace_id, "sampling priority already set and cannot be reassigned");
    }
    return getSamplingPriorityImpl(trace_id);
  }
  if (priority == nullptr) {
    trace.sampling_priority.reset(nullptr);
  } else {
    trace.sampling_priority.reset(new SamplingPriority(*priority));
    if (*priority == SamplingPriority::SamplerDrop || *priority == SamplingPriority::SamplerKeep) {
      // This is an automatically-assigned sampling priority.
      trace.sampling_priority_propagated = true;
    }
  }
  return getSamplingPriorityImpl(trace_id);
}

OptionalSamplingPriority WritingSpanBuffer::assignSamplingPriority(const SpanData* span) {
  std::lock_guard<std::mutex> lock{mutex_};
  return assignSamplingPriorityImpl(span);
}

OptionalSamplingPriority WritingSpanBuffer::assignSamplingPriorityImpl(const SpanData* span) {
  bool sampling_priority_unset = getSamplingPriorityImpl(span->trace_id) == nullptr;
  if (sampling_priority_unset) {
    auto sampler_result = sampler_->sample(span->env(), span->service, span->name, span->trace_id);
    setSamplingPriorityImpl(span->trace_id, std::move(sampler_result.sampling_priority));
    setSamplerResult(span->trace_id, sampler_result);
  }
  return getSamplingPriorityImpl(span->trace_id);
}

void WritingSpanBuffer::setSamplerResult(uint64_t trace_id, SampleResult& sample_result) {
  auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot assign rules sampler result, trace not found");
    return;
  }
  PendingTrace& trace = trace_entry->second;
  trace.sample_result.rule_rate = sample_result.rule_rate;
  trace.sample_result.limiter_rate = sample_result.limiter_rate;
  trace.sample_result.priority_rate = sample_result.priority_rate;
  if (sample_result.sampling_priority != nullptr) {
    trace.sample_result.sampling_priority =
        std::make_unique<SamplingPriority>(*sample_result.sampling_priority);
  }
}
#endif

}  // namespace opentracing
}  // namespace datadog
