#ifndef DD_OPENTRACING_SAMPLING_PRIORITY_H
#define DD_OPENTRACING_SAMPLING_PRIORITY_H

// This component defines an enumeration of "sampling priority" values.
//
// Sampling priority is a hybrid between a sampling decision ("keep" versus
// "drop") and a sampling reason ("user-specified rule").  Values less than or
// equal to zero indicate a decision to "drop," while positive values indicate
// a decision to "keep."
//
// The "priority" in the term "sampling priority" is a misnomer, since the
// value does not denote any relationship among the different kinds of sampling
// decisions.

#include <memory>

namespace datadog {
namespace opentracing {

enum class SamplingPriority : int {
  UserDrop = -1,
  SamplerDrop = 0,
  SamplerKeep = 1,
  UserKeep = 2,

  MinimumValue = UserDrop,
  MaximumValue = UserKeep,
};

// A SamplingPriority that encompasses only values that may be directly set by users.
enum class UserSamplingPriority : int {
  UserDrop = static_cast<int>(SamplingPriority::UserDrop),
  UserKeep = static_cast<int>(SamplingPriority::UserKeep),
};

// Move to std::optional in C++17 when it has better compiler support.
using OptionalSamplingPriority = std::unique_ptr<SamplingPriority>;

OptionalSamplingPriority asSamplingPriority(int i);

}  // namespace opentracing
}  // namespace datadog

#endif
