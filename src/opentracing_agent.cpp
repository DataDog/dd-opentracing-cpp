#include <datadog/opentracing.h>
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  return std::shared_ptr<ot::Tracer>{new Tracer{options}};
}

std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TracePublisher>> makeTracerAndPublisher(
    const TracerOptions &options) {
  auto xwriter = std::make_shared<ExternalWriter>();
  auto publisher = xwriter->publisher();
  std::shared_ptr<Writer> writer = xwriter;
  return std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TracePublisher>>{
      std::shared_ptr<ot::Tracer>{new Tracer{options, writer}}, publisher};
}

}  // namespace opentracing
}  // namespace datadog
