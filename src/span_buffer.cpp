#include "span_buffer.h"

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
  trace.applySamplingDecisionToUpstreamServices();
  span.meta.insert(trace.trace_tags.begin(), trace.trace_tags.end());
  if (!trace.propagation_error.empty()) {
    span.meta[datadog_propagation_error_tag] = trace.propagation_error;
  }
  // Forward to the finisher that applies to all spans (not just root spans).
  finish_span(trace, span);
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
  if (applied_sampling_decision_to_upstream_services || sampling_decision_extracted || sampling_priority == nullptr) {
    // We did not make the sampling decision, or we've already done this.
    return;
  }

  // In unit tests, we sometimes don't have a service name.  In those cases,
  // omit our `UpstreamService` entry (those tests are not looking for the
  // corresponding tag).
  if (service.empty()) {
    return;
  }

  // If we have a sampling priority and `sampling_decision_extracted == false`,
  // then the sampling priority was determined by this tracer, and so we will
  // have set a corresponding sampling mechanism.
  assert(sample_result.sampling_mechanism != nullptr);
  
  UpstreamService this_service;
  this_service.service_name = service;
  this_service.sampling_priority = *sampling_priority;
  this_service.sampling_mechanism = int(sample_result.sampling_mechanism.get<SamplingMechanism>());
  this_service.sampling_rate = sample_result.applied_rate;

  appendUpstreamService(trace_tags[upstream_services_tag], this_service);
  applied_sampling_decision_to_upstream_services = true;
}

SpanBuffer::SpanBuffer(std::shared_ptr<const Logger> logger,
                                     std::shared_ptr<Writer> writer,
                                     std::shared_ptr<RulesSampler> sampler,
                                     SpanBufferOptions options)
    : logger_(logger), writer_(writer), sampler_(sampler), options_(options) {}

void SpanBuffer::registerSpan(const SpanContext& context) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  uint64_t trace_id = context.traceId();
  auto trace_iter = traces_.find(trace_id);
  if (trace_iter == traces_.end() || trace_iter->second.all_spans.empty()) {
    trace_iter = traces_.emplace(trace_id, PendingTrace{logger_, trace_id}).first;
    auto& trace = trace_iter->second;
    // If a sampling priority was extracted, apply it to the pending trace.
    OptionalSamplingPriority p = context.getPropagatedSamplingPriority();
    if (p != nullptr) {
      setSamplingPriorityFromExtractedContext(trace_id, *p);
    }
    // If an origin was extracted, apply it to the pending trace.
    if (!context.origin().empty()) {
      trace.origin = context.origin();
    }
    trace.trace_tags = context.getExtractedTraceTags();
    trace.hostname = options_.hostname;
    trace.analytics_rate = options_.analytics_rate;
    trace.service = options_.service;
  }
  trace_iter->second.all_spans.insert(context.id());
}

void SpanBuffer::finishSpan(std::unique_ptr<SpanData> span) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  auto trace_iter = traces_.find(span->traceId());
  if (trace_iter == traces_.end()) {
    logger_->Log(LogLevel::error, "Missing trace for finished span");
    return;
  }
  auto& trace = trace_iter->second;
  if (trace.all_spans.find(span->spanId()) == trace.all_spans.end()) {
    logger_->Log(LogLevel::error,
                 "A Span that was not registered was submitted to SpanBuffer");
    return;
  }
  uint64_t trace_id = span->traceId();
  trace.finished_spans->push_back(std::move(span));
  if (trace.finished_spans->size() == trace.all_spans.size()) {
    generateSamplingPriorityImpl(trace.finished_spans->back().get());
    trace.finish();
    unbufferAndWriteTrace(trace_id);
  }
}

void SpanBuffer::unbufferAndWriteTrace(uint64_t trace_id) {
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

void SpanBuffer::flush(std::chrono::milliseconds timeout) { writer_->flush(timeout); }

OptionalSamplingPriority SpanBuffer::getSamplingPriority(uint64_t trace_id) const {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return getSamplingPriorityImpl(trace_id);
}
OptionalSamplingPriority SpanBuffer::getSamplingPriorityImpl(uint64_t trace_id) const {
  auto trace = traces_.find(trace_id);
  if (trace == traces_.end()) {
    logger_->Trace(trace_id, "cannot get sampling priority, trace not found");
    return nullptr;
  }
  return clone(trace->second.sampling_priority);
}

OptionalSamplingPriority SpanBuffer::setSamplingPriorityFromUser(uint64_t trace_id, const std::unique_ptr<UserSamplingPriority>& value) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return setSamplingPriorityFromUserImpl(trace_id, value);
}

OptionalSamplingPriority SpanBuffer::setSamplingPriorityFromExtractedContext(uint64_t trace_id, SamplingPriority value) {
  const auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot set sampling priority, trace not found");
    return nullptr;
  }
  PendingTrace& trace = trace_entry->second;
  
  if (trace.sampling_priority_locked) {
    return getSamplingPriorityImpl(trace_id);
  }

  trace.sampling_priority = std::make_unique<SamplingPriority>(value);
  // An upstream service has made a decision -- the user can no longer override it.
  trace.sampling_priority_locked = true;
  trace.sampling_decision_extracted = true;
  // We can't infer the sampling mechanism from the priority, so mechanism will
  // be left null.  Setting the mechanism makes sense when we are the one
  // making the sampling decision.
  
  return getSamplingPriorityImpl(trace_id);
}

OptionalSamplingPriority SpanBuffer::setSamplingPriorityFromUserImpl(uint64_t trace_id, const std::unique_ptr<UserSamplingPriority>& value) {
  const auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot set sampling priority, trace not found");
    return nullptr;
  }
  PendingTrace& trace = trace_entry->second;
  
  if (trace.sampling_priority_locked) {
    logger_->Trace(trace_id, "sampling priority already set and cannot be reassigned");
    return getSamplingPriorityImpl(trace_id);
  }

  trace.sampling_priority = asSamplingPriority(value);
  trace.sampling_decision_extracted = false;
  trace.sample_result.sampling_mechanism = SamplingMechanism::Manual;
  // We do _not_ lock the sampling decision here, because the user could change
  // it again before we need to use it.

  return getSamplingPriorityImpl(trace_id);
}

OptionalSamplingPriority SpanBuffer::setSamplingPriorityFromSampler(uint64_t trace_id, const SampleResult& value) {
  const auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot set sampling priority, trace not found");
    return nullptr;
  }
  PendingTrace& trace = trace_entry->second;
  
  if (trace.sampling_priority_locked) {
    return getSamplingPriorityImpl(trace_id);
  }

  trace.sampling_priority = clone(value.sampling_priority);
  // The sampler has made a decision, but we don't know whether the user will
  // override it before it's needed, so we don't modify
  // `trace.sampling_priority_locked` here.
  trace.sampling_decision_extracted = false;
  
  return getSamplingPriorityImpl(trace_id);
}

OptionalSamplingPriority SpanBuffer::generateSamplingPriority(const SpanData* span) {
  std::lock_guard<std::mutex> lock{mutex_};
  return generateSamplingPriorityImpl(span);
}

OptionalSamplingPriority SpanBuffer::generateSamplingPriorityImpl(const SpanData* span) {
  if (auto sampling_priority = getSamplingPriorityImpl(span->trace_id)) {
    return sampling_priority;
  }

  // Consult the sampler for a decision, save the decision, and then return the
  // saved decision.
  auto sampler_result = sampler_->sample(span->env(), span->service, span->name, span->trace_id);
  setSamplerResult(span->trace_id, sampler_result);
  setSamplingPriorityFromSampler(span->trace_id, sampler_result);
  return getSamplingPriorityImpl(span->trace_id);
}

std::string SpanBuffer::serializeTraceTags(uint64_t trace_id) {
  std::string result;
  std::lock_guard<std::mutex> lock{mutex_};

  const auto trace_found = traces_.find(trace_id);
  if (trace_found == traces_.end()) {
    logger_->Log(LogLevel::error, trace_id,
                 "Requested trace_id not found in SpanBuffer::serializeTraceTags");
    return result;
  }

  auto& trace = trace_found->second;

  trace.applySamplingDecisionToUpstreamServices();
  for (const auto& entry : trace.trace_tags) {
    appendTag(result, entry.first, entry.second);
  }

  if (result.size() > options_.trace_tags_propagation_max_length) {
    trace.propagation_error = "max_size";
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

void SpanBuffer::setServiceName(uint64_t trace_id, ot::string_view service_name) {
  auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot set service name for trace; trace not found");
    return;
  }

  trace_entry->second.service = service_name;
}

void SpanBuffer::setSamplerResult(uint64_t trace_id, const SampleResult& sample_result) {
  auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot assign rules sampler result, trace not found");
    return;
  }
  PendingTrace& trace = trace_entry->second;
  trace.sample_result.rule_rate = sample_result.rule_rate;
  trace.sample_result.limiter_rate = sample_result.limiter_rate;
  trace.sample_result.priority_rate = sample_result.priority_rate;
  trace.sample_result.applied_rate = sample_result.applied_rate;
  trace.sample_result.sampling_priority = clone(sample_result.sampling_priority);
  trace.sample_result.sampling_mechanism = sample_result.sampling_mechanism;
}

void SpanBuffer::lockSamplingPriority(uint64_t trace_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  lockSamplingPriorityImpl(trace_id);
}

void SpanBuffer::lockSamplingPriorityImpl(uint64_t trace_id) {
  const auto trace_entry = traces_.find(trace_id);
  if (trace_entry == traces_.end()) {
    logger_->Trace(trace_id, "cannot lock sampling decision, trace not found");
    return;
  }

  trace_entry->second.sampling_priority_locked = true;
}

}  // namespace opentracing
}  // namespace datadog
