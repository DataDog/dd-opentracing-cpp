#include <datadog/opentracing.h>
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  return std::shared_ptr<ot::Tracer>{new Tracer{options}};
}

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options,
                                       std::shared_ptr<TracePublisher> &publisher) {
  return std::shared_ptr<ot::Tracer>{new Tracer{options, publisher}};
}

}  // namespace opentracing
}  // namespace datadog
