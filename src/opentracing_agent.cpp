#include <datadog/opentracing.h>
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  return std::shared_ptr<ot::Tracer>{new Tracer{options}};
}

}  // namespace opentracing
}  // namespace datadog
