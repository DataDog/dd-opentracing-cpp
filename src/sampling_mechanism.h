#ifndef DD_OPENTRACING_SAMPLING_MECHANISM
#define DD_OPENTRACING_SAMPLING_MECHANISM

// TODO: What is a sampling mechanism?

#include <opentracing/variant/variant.hpp>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// `KnownSamplingMechanism` is a sampling mechanism value with a known
// interpretation.
enum class KnownSamplingMechanism {
  // There are no sampling rules configured, and the tracer has not yet
  // received any rates from the agent.
  Default = 0,
  // The sampling decision was due to a sampling rate conveyed by the agent.
  AgentRate = 1,
  // Reserved for future use.
  RemoteRateAuto = 2,
  // The sampling decision was due to a matching user-specified sampling rule.
  Rule = 3,
  // The sampling decision was made explicitly by the user, who set a sampling
  // priority.
  Manual = 4,
  // Reserved for future use.
  AppSec = 5,
  // Reserved for future use.
  RemoteRateUserDefined = 6

  // Update `asSamplingMechanism` when a new value is added.
};

// `UnknownSamplingMechanism` is a sampling mechanism value that does not have
// a corresponding `KnownSamplingMechanism` value.
struct UnknownSamplingMechanism {
  int value;
};

inline bool operator==(UnknownSamplingMechanism left, UnknownSamplingMechanism right) {
  return left.value == right.value;
}

// `SamplingMechanism` is either one of the known values, above, or some
// unknown value.  This allows us to propagate future sampling mechanism values
// without requiring that all clients first upgrade their tracers.
using SamplingMechanism = ot::util::variant<KnownSamplingMechanism, UnknownSamplingMechanism>;

SamplingMechanism asSamplingMechanism(int value);

int asInt(SamplingMechanism value);

}  // namespace opentracing
}  // namespace datadog

#endif
