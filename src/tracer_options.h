#ifndef DD_TRACER_OPTIONS_H
#define DD_TRACER_OPTIONS_H

#include <datadog/opentracing.h>

#include <opentracing/expected/expected.hpp>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

ot::expected<std::set<PropagationStyle>> asPropagationStyle(const std::vector<std::string>& styles);

// TODO(cgilmour): refactor this so it returns a "finalized options" type.
ot::expected<TracerOptions, const char*> applyTracerOptionsFromEnvironment(
    const TracerOptions& input);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_TRACER_OPTIONS_H
