#include "sample.h"

namespace datadog {
namespace opentracing {

// Constants used for the Knuth hashing, same constants as the Agent.
const double max_trace_id_double = std::numeric_limits<uint64_t>::max();
const uint64_t constant_rate_hash_factor = 1111111111111111111;

const std::string sample_rate_metric_key = "_sample_rate";

SampleProvider KeepAllSampler() {
  return {
      [](uint64_t) { return true; },
      [](std::unique_ptr<ot::Span>&) {},
  };
}

SampleProvider DiscardAllSampler() {
  return {
      [](uint64_t) { return false; },
      [](std::unique_ptr<ot::Span>&) {},
  };
}

SampleProvider ConstantRateSampler(double rate) {
  uint64_t applied_rate = uint64_t(rate * max_trace_id_double);
  return {
      [=](uint64_t id) {
        uint64_t hashed_id = id * constant_rate_hash_factor;
        return hashed_id < applied_rate;
      },
      [&, rate](std::unique_ptr<ot::Span>& span) { span->SetTag(sample_rate_metric_key, rate); },
  };
}

}  // namespace opentracing
}  // namespace datadog
