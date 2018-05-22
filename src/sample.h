#ifndef DD_OPENTRACING_SAMPLE_H
#define DD_OPENTRACING_SAMPLE_H

#include <functional>
#include <iostream>
#include <limits>

#include <opentracing/tracer.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

typedef struct {
  std::function<bool(uint64_t)> sample;
  std::function<void(std::unique_ptr<ot::Span>&)> tag;
} SampleProvider;

SampleProvider KeepAllSampler();
SampleProvider DiscardAllSampler();
SampleProvider ConstantRateSampler(double rate);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SAMPLE_H
