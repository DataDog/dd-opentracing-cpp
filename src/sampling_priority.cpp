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

OptionalSamplingPriority asSamplingPriority(const std::unique_ptr<UserSamplingPriority>& userPriority) {
  if (userPriority == nullptr) {
    return nullptr;
  } else {
    return asSamplingPriority(int(*userPriority));
  }
}

OptionalSamplingPriority clone(const OptionalSamplingPriority& priority) {
  if (priority == nullptr) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(*priority);
}

}  // namespace opentracing
}  // namespace datadog
