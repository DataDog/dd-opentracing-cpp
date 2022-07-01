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
// To allow forward compatibility with future `SamplingMechanism` values,
// sampling mechanism is treated as just an integer when being deserialized or
// serialized.  `SamplingMechanism` enumerates integer values relevant to logic
// within the tracer.

#include <cstddef>
#include <opentracing/variant/variant.hpp>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// `SamplingMechanism` is a reason for a trace to be dropped or kept.
enum class SamplingMechanism {
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
  RemoteRateUserDefined = 6,
  // Reserved for future use.
  RemoteRateEmergency = 7,
  // Individual span kept by a matching span sampling rule when the enclosing
  // trace was dropped.
  SpanRule = 8
};

// `OptionalSamplingMechanism` is either a `SamplingMechanism` or "empty,"
// indicated by the type `std::nullptr_t`.
using OptionalSamplingMechanism = ot::util::variant<std::nullptr_t, SamplingMechanism>;

}  // namespace opentracing
}  // namespace datadog

#endif
