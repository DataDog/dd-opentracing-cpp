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

class Span;

class SampleProvider {
 public:
  SampleProvider(){};
  virtual ~SampleProvider() {}

  virtual bool discard(const SpanContext& context) const = 0;
  virtual OptionalSamplingPriority sample(const std::string& environment,
                                          const std::string& service, uint64_t trace_id) const = 0;
};

class DiscardRateSampler : public SampleProvider {
 public:
  DiscardRateSampler(double rate);
  ~DiscardRateSampler() override {}

  bool discard(const SpanContext& context) const override;
  OptionalSamplingPriority sample(const std::string& environment, const std::string& service,
                                  uint64_t trace_id) const override;

 protected:
  uint64_t max_trace_id_ = 0;
};

class KeepAllSampler : public DiscardRateSampler {
 public:
  KeepAllSampler() : DiscardRateSampler(0) {}
};

class DiscardAllSampler : public DiscardRateSampler {
 public:
  DiscardAllSampler() : DiscardRateSampler(1) {}
};

class PrioritySampler : public SampleProvider {
 public:
  PrioritySampler() {}
  ~PrioritySampler() override {}

  virtual bool discard(const SpanContext& context) const override;
  virtual OptionalSamplingPriority sample(const std::string& environment,
                                          const std::string& service,
                                          uint64_t trace_id) const override;
  virtual void configure(json config);

 private:
  std::map<std::string, uint64_t> max_hash_by_service_env_{
      {"service:,env:", std::numeric_limits<uint64_t>::max()}};
  mutable std::mutex mutex_;
};

std::shared_ptr<SampleProvider> sampleProviderFromOptions(const TracerOptions& options);

struct RuleResult {
  bool matched;
  double rate;
};

using RuleFunc = std::function<RuleResult(const std::string&, const std::string&)>;

struct SampleResult {
  bool rules_sampling_applied;
  double applied_rate;
  double limiter_rate;
  OptionalSamplingPriority sampling_priority;
};

class RulesSampler {
 public:
  RulesSampler();
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
