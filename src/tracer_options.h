#ifndef DD_TRACER_OPTIONS_H
#define DD_TRACER_OPTIONS_H

#include <datadog/opentracing.h>

#include <opentracing/expected/expected.hpp>
#include <set>
#include <string>
#include <vector>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Return the set of `PropagationStyle`s indicated by `styles`, where `styles`
// contains the names of one or more propagation styles.  Return an unexpected
// value if an error occurs.
ot::expected<std::set<PropagationStyle>> asPropagationStyle(
    const std::vector<std::string>& styles);

// TODO(cgilmour): refactor this so it returns a "finalized options" type.
ot::expected<TracerOptions, std::string> applyTracerOptionsFromEnvironment(
    const TracerOptions& input);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_TRACER_OPTIONS_H
