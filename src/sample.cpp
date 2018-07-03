#include "sample.h"

namespace datadog {
namespace opentracing {

// Constant used to mark traces that have been sampled
const std::string sample_type_baggage_key = "_sample_type";

// Constants used for the Knuth hashing, same constants as the Agent.
const double max_trace_id_double = std::numeric_limits<uint64_t>::max();
const uint64_t constant_rate_hash_factor = 1111111111111111111;

const std::string sample_rate_metric_key = "_sample_rate";

SampleProvider KeepAllSampler() {
  return {
      [](SpanContext& context) {
        context.setBaggageItem(sample_type_baggage_key, "KeepAllSampler");
        return true;
      },
      [](std::unique_ptr<ot::Span>&) {},
  };
}

SampleProvider DiscardAllSampler() {
  return {
      [](SpanContext& context) {
        context.setBaggageItem(sample_type_baggage_key, "DiscardAllSampler");
        return false;
      },
      [](std::unique_ptr<ot::Span>&) {},
  };
}

SampleProvider ConstantRateSampler(double rate) {
  uint64_t max_trace_id = 0;
  // This check is required to avoid undefined behaviour converting the rate back from
  // double to uint64_t.
  if (rate == 1.0) {
    max_trace_id = std::numeric_limits<uint64_t>::max();
  } else if (rate > 0.0) {
    max_trace_id = uint64_t(rate * max_trace_id_double);
  }

  return {
      [=](SpanContext& context) {
        if (context.baggageItem(sample_type_baggage_key) != std::string{}) {
          // Context already marked as sampled, so this span should be traced.
          return true;
        }

        if (context.id() != context.trace_id()) {
          // Permit existing traces. Sampling decision was made upstream.
          return true;
        }

        // A new trace, so a sampling decision needs to be made.
        uint64_t hashed_id = context.trace_id() * constant_rate_hash_factor;
        if (hashed_id < max_trace_id) {
          // Chosen for sampling. Add mark to context and return.
          context.setBaggageItem(sample_type_baggage_key, "ConstantRateSampler");
          return true;
        }

        // Not chosen for sampling.
        return false;
      },
      [&, rate](std::unique_ptr<ot::Span>& span) { span->SetTag(sample_rate_metric_key, rate); },
  };
}

}  // namespace opentracing
}  // namespace datadog
