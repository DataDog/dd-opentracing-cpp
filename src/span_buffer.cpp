#include "span_buffer.h"

#include "overload.h"
#include "sample.h"
#include "span.h"
#include "tag_propagation.h"
#include "writer.h"

namespace datadog {
namespace opentracing {

namespace {
const std::string sampling_priority_metric = "_sampling_priority_v1";
const std::string datadog_origin_tag = "_dd.origin";
const std::string datadog_hostname_tag = "_dd.hostname";
const std::string datadog_upstream_services_tag = "_dd.p.upstream_services";
const std::string datadog_propagation_error_tag = "_dd.propagation_error";
const std::string event_sample_rate_metric = "_dd1.sr.eausr";
const std::string rules_sampler_applied_rate = "_dd.rule_psr";
const std::string rules_sampler_limiter_rate = "_dd.limit_psr";
const std::string priority_sampler_applied_rate = "_dd.agent_psr";

// Return whether the specified `span` is without a parent among the specified
// `all_spans_in_trace`.
bool is_root(const SpanData& span, const std::unordered_set<uint64_t>& all_spans_in_trace) {
  return
      // root span
      span.parent_id == 0 ||
      // local root span of a distributed trace
      all_spans_in_trace.find(span.parent_id) == all_spans_in_trace.end();
}

// Alter the specified `span` to prepare it for encoding with the specified
// `trace`.
void finish_span(const PendingTrace& trace, SpanData& span) {
  // Propagate the trace origin in every span, if present.  This allows, for
  // example, sampling to vary with the trace's stated origin.
  if (!trace.origin.empty()) {
    span.meta[datadog_origin_tag] = trace.origin;
  }
}

// Alter the specified root (i.e. having no parent in the local trace) `span`
// to prepare it for encoding with the specified `trace`.
void finish_root_span(PendingTrace& trace, SpanData& span) {
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
  // If there's a sampling decision, make sure that `trace.upstream_services`
  // reflects the sampling decision, and include `trace.upstream_services` as a
  // tag if it's nonempty.
  if (trace.sampling_priority != nullptr) {
    trace.applySamplingDecisionToUpstreamServices();
  }
  std::string upstream_services = serializeUpstreamServices(trace.upstream_services);
  if (!upstream_services.empty()) {
    span.meta[datadog_upstream_services_tag] = std::move(upstream_services);
  }
  // If there was previously an error during context propagation, note that in
  // a tag.
  if (!trace.propagation_error.empty()) {
    span.meta[datadog_propagation_error_tag] = trace.propagation_error;
  }
  // Forward to the finisher that applies to all spans (not just root spans).
  finish_span(trace, span);
}

// Return the rate from within the specified `sample_result` that applied in
// the sampling decision, or return `std::nan("")` if no rate applied.
double pickSamplingRate(const SampleResult& sample_result) {
  return apply_visitor(overload([](UnknownSamplingMechanism) { return std::nan(""); },
                                [&](KnownSamplingMechanism reason) {
                                  switch (reason) {
                                    case KnownSamplingMechanism::Default:
                                      return sample_result.priority_rate;
                                    case KnownSamplingMechanism::AgentRate:
                                      return sample_result.priority_rate;
                                    case KnownSamplingMechanism::RemoteRateAuto:
                                      return std::nan("");
                                    case KnownSamplingMechanism::Rule:
                                      return sample_result.rule_rate;
                                    case KnownSamplingMechanism::Manual:
                                      return std::nan("");
                                    case KnownSamplingMechanism::AppSec:
                                      return std::nan("");
                                    case KnownSamplingMechanism::RemoteRateUserDefined:
                                      return std::nan("");
                                  }
                                  // unreachable (but difficult to prove)
                                  return std::nan("");
                                }),
                       sample_result.sampling_mechanism);
}

}  // namespace

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

void PendingTrace::applySamplingDecisionToUpstreamServices() {
  // This is a precondition.
  assert(sampling_priority);
  // This is true until this trace is finished and written by the span buffer.
  assert(finished_spans);

  if (!upstream_services.empty() &&
      upstream_services.back().sampling_priority == *sampling_priority) {
    // Our sampling decision is the same as the previous guy's, so we have
    // nothing to add.
    return;
  }

  // Either we're the first to make a sampling decision, or our decision
  // differs from the previous service's.  Append a record for this service.

  // In unit tests, we sometimes don't have a service name.  In those cases,
  // omit our `UpstreamService` entry (those tests are not looking for the
  // corresponding tag).
  if (service.empty()) {
    return;
  }

  UpstreamService this_service;
  this_service.service_name = service;
  this_service.sampling_priority = *sampling_priority;
  this_service.sampling_mechanism = sample_result.sampling_mechanism;
  this_service.sampling_rate = pickSamplingRate(sample_result);

  upstream_services.push_back(std::move(this_service));
}

WritingSpanBuffer::WritingSpanBuffer(std::shared_ptr<const Logger> logger,
                                     std::shared_ptr<Writer> writer,
                                     std::shared_ptr<RulesSampler> sampler,
                                     WritingSpanBufferOptions options)
    : logger_(logger), writer_(writer), sampler_(sampler), options_(options) {}

void WritingSpanBuffer::registerSpan(const SpanContext& context) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = context.traceId();
  auto trace_iter = traces_.find(trace_id);
  if (trace_iter == traces_.end() || trace_iter->second.all_spans.empty()) {
    trace_iter = traces_.emplace(trace_id, PendingTrace{logger_, trace_id}).first;
    auto& trace = trace_iter->second;
    // If a sampling priority was extracted, apply it to the pending trace.
    OptionalSamplingPriority p = context.getPropagatedSamplingPriority();
    trace.sampling_priority_locked = p != nullptr;
    trace.sampling_priority = std::move(p);
    // If an origin was extracted, apply it to the pending trace.
    if (!context.origin().empty()) {
      trace.origin = context.origin();
    }
    trace.trace_tags = context.getExtractedTraceTags();
    trace.upstream_services = context.getExtractedUpstreamServices();

    if (trace.sampling_priority_locked) {
      // A sampling decision has been made. Let `trace.upstream_services`
      // reflect our decision, if it is the first decision or is different from
      // the one before.
      trace.applySamplingDecisionToUpstreamServices();
    }

    trace.hostname = options_.hostname;
    trace.analytics_rate = options_.analytics_rate;
    trace.service = options_.service;
  }
  trace_iter->second.all_spans.insert(context.id());
}

void WritingSpanBuffer::finishSpan(std::unique_ptr<SpanData> span) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  auto trace_iter = traces_.find(span->traceId());
  if (trace_iter == traces_.end()) {
    logger_->Log(LogLevel::error, "Missing trace for finished span");
    return;
  }
  auto& trace = trace_iter->second;
  if (trace.all_spans.find(span->spanId()) == trace.all_spans.end()) {
    logger_->Log(LogLevel::error,
                 "A Span that was not registered was submitted to WritingSpanBuffer");
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
  if (trace.sampling_priority_locked) {
    if (priority == nullptr || *priority == SamplingPriority::UserKeep ||
        *priority == SamplingPriority::UserDrop) {
      // Only print a diagnostic if the sampling decision is due to a setting
      // that the user set in the tracer. This case is legitimate (albeit with
      // the same outcome) if the Sampler itself is trying to
      // assignSamplingPriority.
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
      trace.sampling_priority_locked = true;
      // We made a sampling decision.  Might need to indicate that in
      // `trace.upstream_services`.
      trace.applySamplingDecisionToUpstreamServices();
    } else if (*priority == SamplingPriority::UserDrop ||
               *priority == SamplingPriority::UserKeep) {
      trace.sample_result.sampling_mechanism = KnownSamplingMechanism::Manual;
    }
  }
  return getSamplingPriorityImpl(trace_id);
}

OptionalSamplingPriority WritingSpanBuffer::assignSamplingPriority(const SpanData* span) {
  std::lock_guard<std::mutex> lock{mutex_};
  return assignSamplingPriorityImpl(span);
}

OptionalSamplingPriority WritingSpanBuffer::assignSamplingPriorityImpl(const SpanData* span) {
  if (auto sampling_priority = getSamplingPriorityImpl(span->trace_id)) {
    return sampling_priority;
  }

  // Consult the sampler for a decision, save the decision, and then return the
  // saved decision.
  auto sampler_result = sampler_->sample(span->env(), span->service, span->name, span->trace_id);
  setSamplerResult(span->trace_id, sampler_result);
  setSamplingPriorityImpl(span->trace_id, std::move(sampler_result.sampling_priority));
  return getSamplingPriorityImpl(span->trace_id);
}

std::string WritingSpanBuffer::serializeTraceTags(uint64_t trace_id) {
  std::string result;
  std::unique_lock<std::mutex> lock{mutex_};

  const auto trace_found = traces_.find(trace_id);
  if (trace_found == traces_.end()) {
    lock.unlock();
    logger_->Log(LogLevel::error, trace_id,
                 "Requested trace_id not found in WritingSpanBuffer::serializeTraceTags");
    return result;
  }

  auto& trace = trace_found->second;

  if (!trace.upstream_services.empty()) {
    appendTag(result, upstream_services_tag, serializeUpstreamServices(trace.upstream_services));
  }
  for (const auto& entry : trace.trace_tags) {
    appendTag(result, entry.first, entry.second);
  }

  if (result.size() > options_.trace_tags_propagation_max_length) {
    trace.propagation_error = "max_size";
    lock.unlock();
    std::ostringstream message;
    message
        << "Serialized trace tags are too large for propagation.  Configured maximum length is "
        << options_.trace_tags_propagation_max_length << ", but the following has length "
        << result.size() << ": " << result;
    logger_->Log(LogLevel::error, trace_id, message.str());
    // Return an empty string, which will not be propagated.
    result.clear();
  }

  return result;
}

void WritingSpanBuffer::setSamplerResult(uint64_t trace_id, const SampleResult& sample_result) {
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

}  // namespace opentracing
}  // namespace datadog
