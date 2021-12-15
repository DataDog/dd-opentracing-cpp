#ifndef DD_OPENTRACING_SAMPLING_MECHANISM
#define DD_OPENTRACING_SAMPLING_MECHANISM

// This component provides a type, `SamplingMechanism`, describing a reason for
// a sampling decision.  A sampler (or a user, with a manual override) decides
// whether to keep or to drop a trace, but it might do so for various reasons.
//
// Some of those reasons are indicated by existing `SamplingPriority` values,
// but `SamplingPriority` is inadequate for future expansion, for two reasons:
//
// - `SamplingPriority` conflates the keep/drop decision with the reason (e.g.
//   `UserKeep` vs. `SamplerKeep`).  Some engineers dislike this.
// - Some tracer implementations (including this one) do not decode
//   `SamplingPriority` integer values outside of those enumerated in this
//   library.  This makes adding new values infeasible, as older versions of
//   tracers propagating the `SamplingPriority` along the trace will omit new
//   integer values.
//
// `SamplingMechanism` is a redefinition of the "why" of a sampling decision,
// while the "what" is still the binary keep/drop.
//
// Since `SamplingPriority` is already in use and has implications for sampling
// behavior (both in its propagation along and trace and its interpretation by
// the trace agent), the combination `{SamplingPriority, SamplingMechanism}` is
// used to completely describe a sampling decision.  The `SamplingPriority`
// conveys the keep/drop decision, as well as the existing (and now redundant)
// user vs. sampler distinction, while the `SamplingMechanism` conveys
// precisely where the sampling decision came from, e.g. a user-specified
// sampling rule, a user-specified override, an agent-specified priority
// sampling rate, etc.
//
// To allow forward compatibility with future `SamplingMechanism` values, the
// `SamplingMechanism` type is defined as a variant between
// `KnownSamplingMechanism`, which is an enumeration, and
// `UnknownSamplingMechanism`, which contains any other integer.
//
// `SamplingMechanism` instances can be inspected by visitation using the
// `::opentracing::apply_visitor` function template, which can be invoked
// unqualified due to argument dependent lookup.
//
// For example:
// clang-format off
//
//     void echo(SamplingMechanism reason) {
//       apply_visitor(overload(
//                         [](KnownSamplingMechanism reason) {
//                           std::cout << "It has one of the known values: " << int(reason);
//                         },
//                         [](UnknownSamplingMechanism reason) {
//                           std::cout << "It has some other, probably future, value: " << reason.value;
//                         }),
//                     reason);
//       std::cout << '\n';
//     }
//
// clang-format on

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

  // Note: Update `asSamplingMechanism` when a new value is added.
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
