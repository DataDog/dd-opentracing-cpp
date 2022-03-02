#include "pending_trace.h"

#include <cassert>

#include "span.h"

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
  // Forward to the finisher that applies to all spans (not just root spans).
  finish_span(trace, span);
}

}  // namespace

PendingTrace::PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id)
    : logger(logger),
      trace_id(trace_id),
      finished_spans(TraceData{new std::vector<std::unique_ptr<SpanData>>()}),
      all_spans() {}

PendingTrace::PendingTrace(std::shared_ptr<const Logger> logger, uint64_t trace_id,
                           std::unique_ptr<SamplingPriority> sampling_priority)
    : logger(logger),
      trace_id(trace_id),
      finished_spans(TraceData{new std::vector<std::unique_ptr<SpanData>>()}),
      all_spans(),
      sampling_priority(std::move(sampling_priority)) {}

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
  if (applied_sampling_decision_to_upstream_services || sampling_decision_extracted ||
      sampling_priority == nullptr) {
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

}  // namespace opentracing
}  // namespace datadog
