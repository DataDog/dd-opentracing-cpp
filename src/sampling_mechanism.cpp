#include "sampling_mechanism.h"

#include "overload.h"

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
  return apply_visitor(overload([](UnknownSamplingMechanism reason) { return reason.value; },
                                [](KnownSamplingMechanism reason) { return int(reason); }),
                       value);
}

}  // namespace opentracing
}  // namespace datadog
