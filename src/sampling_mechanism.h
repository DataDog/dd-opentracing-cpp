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
  Default = 0,
  AgentRate = 1,
  RemoteRateAuto = 2,
  Rule = 3,
  Manual = 4,
  AppSec = 5,
  RemoteRateUserDefined = 6
  // Update `asSamplingMechanism` when a new value is added.
};

// `UnknownSamplingMechanism` is a sampling mechanism value that does not have
// a corresponding `KnownSamplingMechanism` value.
struct UnknownSamplingMechanism {
  int value;
  operator int() const { return value; }
};

// `SamplingMechanism` is either one of the known values, above, or some
// unknown value.  This allows us to propagate future sampling mechanism values
// without requiring that all clients first upgrade their tracers.
using SamplingMechanism = ot::util::variant<KnownSamplingMechanism, UnknownSamplingMechanism>;

SamplingMechanism asSamplingMechanism(int value);

int asInt(SamplingMechanism value);

}  // namespace opentracing
}  // namespace datadog

#endif
