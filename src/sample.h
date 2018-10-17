#ifndef DD_OPENTRACING_SAMPLE_H
#define DD_OPENTRACING_SAMPLE_H

#include <datadog/opentracing.h>
#include <opentracing/tracer.h>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
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

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SAMPLE_H
