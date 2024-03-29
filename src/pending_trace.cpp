#include "pending_trace.h"

#include <cassert>

#include "span.h"

namespace datadog {
namespace opentracing {
namespace {

const std::string sampling_priority_metric = "_sampling_priority_v1";
const std::string datadog_origin_tag = "_dd.origin";
const std::string datadog_hostname_tag = "_dd.hostname";
const std::string datadog_decision_maker_tag = "_dd.p.dm";
const std::string datadog_propagation_error_tag = "_dd.propagation_error";
const std::string event_sample_rate_metric = "_dd1.sr.eausr";
const std::string rules_sampler_applied_rate = "_dd.rule_psr";
const std::string rules_sampler_limiter_rate = "_dd.limit_psr";
const std::string priority_sampler_applied_rate = "_dd.agent_psr";
const std::string span_sampling_mechanism = "_dd.span_sampling.mechanism";
const std::string span_sampling_rule_rate = "_dd.span_sampling.rule_rate";
const std::string span_sampling_limit = "_dd.span_sampling.max_per_second";

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
  trace.applySamplingDecisionToTraceTags();
  span.meta.insert(trace.trace_tags.begin(), trace.trace_tags.end());
  if (!trace.propagation_error.empty()) {
    span.meta[datadog_propagation_error_tag] = trace.propagation_error;
  }
  // Forward to the finisher that applies to all spans (not just root spans).
  finish_span(trace, span);
}

// Determine whether the specified `span` matches a rule in the specified
// `span_sampler` and the sampling decision of that rule is to keep the `span`.
// If so, then add appropriate tags to `span`.
void apply_span_sampling(SpanSampler& span_sampler, SpanData& span) {
  SpanSampler::Rule* const rule = span_sampler.match(span);
  if (!rule || !rule->sample(span)) {
    return;
  }

  // The span matched a span rule, and the rule decided to keep the span.
  // Add span-sampling-specific tags to the span.
  span.metrics[span_sampling_mechanism] = int(SamplingMechanism::SpanRule);
  span.metrics[span_sampling_rule_rate] = rule->config().sample_rate;
  const double limit = rule->config().max_per_second;
  if (!std::isnan(limit)) {
    span.metrics[span_sampling_limit] = limit;
  }
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

void PendingTrace::finish(SpanSampler* span_sampler) {
  // Apply changes to spans, in particular treating the root / local-root
  // span as special.
  for (const auto& span : *finished_spans) {
    if (is_root(*span, all_spans)) {
      finish_root_span(*this, *span);
    } else {
      finish_span(*this, *span);
    }
  }

  // If we have span sampling rules and are dropping the trace, see if any
  // span sampling tags need to be added.
  if (span_sampler && !span_sampler->rules().empty() && sampling_priority &&
      int(*sampling_priority) <= 0) {
    for (const auto& span : *finished_spans) {
      apply_span_sampling(*span_sampler, *span);
    }
  }
}

void PendingTrace::applySamplingDecisionToTraceTags() {
  if (sampling_decision_extracted || sampling_priority == nullptr) {
    // We did not make the sampling decision.
    return;
  }

  // In unit tests, we sometimes don't have a service name.  In those cases,
  // omit the tag (those tests are not looking for the corresponding tag).
  if (service.empty()) {
    return;
  }

  // If we have a sampling priority and `sampling_decision_extracted == false`,
  // then the sampling priority was determined by this tracer, and so we will
  // have set a corresponding sampling mechanism.
  assert(sample_result.sampling_mechanism != nullptr);

  // The "decision maker" is formatted as:
  //
  //     <maybe someday service name hashed> "-" <sampling mechanism>
  //
  // So for now it's just
  //
  //     "-" <sampling mechanism>
  //
  // e.g.
  //
  //     -4
  //
  // That's a separating hyphen, not a minus sign.
  const int mechanism = int(sample_result.sampling_mechanism.get<SamplingMechanism>());
  trace_tags[datadog_decision_maker_tag] = "-" + std::to_string(mechanism);
}

}  // namespace opentracing
}  // namespace datadog
