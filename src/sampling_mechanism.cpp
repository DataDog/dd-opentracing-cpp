#include "sampling_mechanism.h"

namespace datadog {
namespace opentracing {

SamplingMechanism asSamplingMechanism(int value) {
  if (value >= int(KnownSamplingMechanism::Default) &&
      value <= int(KnownSamplingMechanism::RemoteRateUserDefined)) {
    return KnownSamplingMechanism(value);
  }
  return UnknownSamplingMechanism{value};
}

int asInt(SamplingMechanism value) {
  return apply_visitor([](auto value) { return int(value); }, value);
}

}  // namespace opentracing
}  // namespace datadog
