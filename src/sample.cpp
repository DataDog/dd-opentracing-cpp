#include "sample.h"
#include <sstream>

namespace datadog {
namespace opentracing {

// Constant used to mark traces that have been sampled
const std::string sample_type_baggage_key = "_sample_type";

// Constants used for the Knuth hashing, same constants as the Agent.
constexpr double max_trace_id_double = static_cast<double>(std::numeric_limits<uint64_t>::max());
constexpr uint64_t constant_rate_hash_factor = UINT64_C(1111111111111111111);

const std::string sample_rate_metric_key = "_sample_rate";

namespace {
uint64_t maxIdFromKeepRate(double rate) {
  // This check is required to avoid undefined behaviour converting the rate back from
  // double to uint64_t.
  if (rate == 1.0) {
    return std::numeric_limits<uint64_t>::max();
  } else if (rate > 0.0) {
    return uint64_t(rate * max_trace_id_double);
  }
  return 0;
}
}  // namespace

DiscardRateSampler::DiscardRateSampler(double rate)
    : max_trace_id_(maxIdFromKeepRate(1.0 - rate)) {}

bool DiscardRateSampler::discard(const SpanContext& context) const {
  // I don't know how voodoo it is to use the trace_id essentially as a source of randomness,
  // rather than generating a new random number here. It's a bit faster, and more importantly it's
  // cargo-culted from the agent. However it does still seem too "clever", and makes testing a
  // bit awkward.
  uint64_t hashed_id = context.traceId() * constant_rate_hash_factor;
  if (hashed_id < max_trace_id_) {
    return false;
  }
  return true;
}

OptionalSamplingPriority DiscardRateSampler::sample(const std::string& /* environment */,
                                                    const std::string& /* service */,
                                                    uint64_t /* trace_id */) const {
  return nullptr;
}

bool PrioritySampler::discard(const SpanContext& /* context */) const { return false; }

OptionalSamplingPriority PrioritySampler::sample(const std::string& environment,
                                                 const std::string& service,
                                                 uint64_t trace_id) const {
  uint64_t max_hash = std::numeric_limits<uint64_t>::max();
  std::ostringstream key;
  key << "service:" << service << ",env:" << environment;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto const rule = max_hash_by_service_env_.find(key.str());
    if (rule != max_hash_by_service_env_.end()) {
      max_hash = rule->second;
    }
  }
  // I don't know how voodoo it is to use the trace_id essentially as a source of randomness,
  // rather than generating a new random number here. It's a bit faster, and more importantly it's
  // cargo-culted from the agent. However it does still seem too "clever", and makes testing a
  // bit awkward.
  uint64_t hashed_id = trace_id * constant_rate_hash_factor;
  if (hashed_id >= max_hash) {
    return std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
  }

  return std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
}

void PrioritySampler::configure(json config) {
  std::lock_guard<std::mutex> lock{mutex_};
  max_hash_by_service_env_.clear();
  for (json::iterator it = config.begin(); it != config.end(); ++it) {
    max_hash_by_service_env_[it.key()] = maxIdFromKeepRate(it.value());
  }
}

std::shared_ptr<SampleProvider> sampleProviderFromOptions(const TracerOptions& options) {
  if (options.priority_sampling) {
    return std::shared_ptr<SampleProvider>{new PrioritySampler()};
  }
  return std::shared_ptr<SampleProvider>{new DiscardRateSampler(1.0 - options.sample_rate)};
}

}  // namespace opentracing
}  // namespace datadog
