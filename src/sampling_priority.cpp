#include "sampling_priority.h"

namespace datadog {
namespace opentracing {

OptionalSamplingPriority asSamplingPriority(int i) {
  if (i < static_cast<int>(SamplingPriority::MinimumValue) ||
      i > static_cast<int>(SamplingPriority::MaximumValue)) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(static_cast<SamplingPriority>(i));
}

}  // namespace opentracing
}  // namespace datadog
