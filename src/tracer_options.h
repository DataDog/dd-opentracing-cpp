#ifndef DD_TRACER_OPTIONS_H
#define DD_TRACER_OPTIONS_H

#include <datadog/opentracing.h>
#include <opentracing/expected/expected.hpp>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

template <class Iterable>
ot::expected<std::set<PropagationStyle>> asPropagationStyle(Iterable styles) {
  std::set<PropagationStyle> propagation_styles;
  for (const std::string& style : styles) {
    if (style == "Datadog") {
      propagation_styles.insert(PropagationStyle::Datadog);
    } else if (style == "B3") {
      propagation_styles.insert(PropagationStyle::B3);
    } else {
      return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  if (propagation_styles.size() == 0) {
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  return propagation_styles;
};

ot::expected<TracerOptions, const char*> applyTracerOptionsFromEnvironment(
    const TracerOptions& input);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_TRACER_OPTIONS_H
