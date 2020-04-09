#ifndef DD_OPENTRACING_SAMPLE_H
#define DD_OPENTRACING_SAMPLE_H

#include <datadog/opentracing.h>
#include <opentracing/tracer.h>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "limiter.h"
#include "propagation.h"

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

struct SampleResult {
  double rule_rate = std::nan("");
  double limiter_rate = std::nan("");
  double priority_rate = std::nan("");
  OptionalSamplingPriority sampling_priority = nullptr;
};

struct SamplingRate {
  double rate = std::nan("");
  uint64_t max_hash = 0;
};

class PrioritySampler {
 public:
  PrioritySampler() : default_sample_rate_{1.0, std::numeric_limits<uint64_t>::max()} {}
  virtual ~PrioritySampler() {}

  virtual SampleResult sample(const std::string& environment, const std::string& service,
                              uint64_t trace_id) const;
  virtual void configure(json config);

 private:
  mutable std::mutex mutex_;
  std::map<std::string, SamplingRate> agent_sampling_rates_;
  SamplingRate default_sample_rate_;
};

struct RuleResult {
  bool matched = false;
  double rate = std::nan("");
};

using RuleFunc = std::function<RuleResult(const std::string&, const std::string&)>;

class RulesSampler {
 public:
  RulesSampler();
  RulesSampler(TimeProvider clock, long max_tokens, double refresh_rate, long tokens_per_refresh);
  virtual ~RulesSampler() {}
  void addRule(RuleFunc f);
  virtual SampleResult sample(const std::string& environment, const std::string& service,
                              const std::string& name, uint64_t trace_id);
  virtual RuleResult match(const std::string& service, const std::string& name) const;
  virtual void updatePrioritySampler(json config);

 private:
  Limiter sampling_limiter_;
  std::vector<RuleFunc> sampling_rules_;
  PrioritySampler priority_sampler_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SAMPLE_H
