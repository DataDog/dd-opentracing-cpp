#include "sample.h"
#include <sstream>

namespace datadog {
namespace opentracing {

namespace {
// Constants used for the Knuth hashing, same constants as the Agent.
constexpr double max_trace_id_double = static_cast<double>(std::numeric_limits<uint64_t>::max());
constexpr uint64_t constant_rate_hash_factor = UINT64_C(1111111111111111111);

const std::string priority_sampler_default_rate_key = "service:,env:";

uint64_t maxIdFromSampleRate(double rate) {
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

SampleResult PrioritySampler::sample(const std::string& environment, const std::string& service,
                                     uint64_t trace_id) const {
  SamplingRate applied_rate = default_sample_rate_;
  std::ostringstream key;
  key << "service:" << service << ",env:" << environment;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto const rule = agent_sampling_rates_.find(key.str());
    if (rule != agent_sampling_rates_.end()) {
      applied_rate = rule->second;
    }
  }
  // I don't know how voodoo it is to use the trace_id essentially as a source of randomness,
  // rather than generating a new random number here. It's a bit faster, and more importantly it's
  // cargo-culted from the agent. However it does still seem too "clever", and makes testing a
  // bit awkward.
  uint64_t hashed_id = trace_id * constant_rate_hash_factor;
  SampleResult result;
  result.priority_rate = applied_rate.rate;
  if (hashed_id >= applied_rate.max_hash) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
  } else {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
  }
  return result;
}

void PrioritySampler::configure(json config) {
  std::lock_guard<std::mutex> lock{mutex_};
  agent_sampling_rates_.clear();
  for (json::iterator it = config.begin(); it != config.end(); ++it) {
    auto key = it.key();
    auto rate = it.value();
    auto max_hashed = maxIdFromSampleRate(rate);
    if (key == priority_sampler_default_rate_key) {
      default_sample_rate_ = {rate, max_hashed};
    } else {
      agent_sampling_rates_[key] = {rate, max_hashed};
    }
  }
}

RulesSampler::RulesSampler() : sampling_limiter_(getRealTime, 100, 100.0, 1) {}

RulesSampler::RulesSampler(TimeProvider clock, long max_tokens, double refresh_rate,
                           long tokens_per_refresh)
    : sampling_limiter_(clock, max_tokens, refresh_rate, tokens_per_refresh) {}

void RulesSampler::addRule(RuleFunc f) { sampling_rules_.push_back(f); }

SampleResult RulesSampler::sample(const std::string& environment, const std::string& service,
                                  const std::string& name, uint64_t trace_id) {
  auto rule_result = match(service, name);
  if (!rule_result.matched) {
    return priority_sampler_.sample(environment, service, trace_id);
  }

  SampleResult result;
  result.rule_rate = rule_result.rate;
  auto max_hash = maxIdFromSampleRate(rule_result.rate);
  uint64_t hashed_id = trace_id * constant_rate_hash_factor;
  if (hashed_id >= max_hash) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
    return result;
  }

  auto limit_result = sampling_limiter_.allow();
  result.limiter_rate = limit_result.effective_rate;
  if (limit_result.allowed) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
  } else {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
  }
  return result;
}

RuleResult RulesSampler::match(const std::string& service, const std::string& name) const {
  static auto nan = std::nan("");
  for (auto& rule : sampling_rules_) {
    auto result = rule(service, name);
    if (result.matched) {
      return result;
    }
  }
  return {false, nan};
}

void RulesSampler::updatePrioritySampler(json config) { priority_sampler_.configure(config); }

}  // namespace opentracing
}  // namespace datadog
