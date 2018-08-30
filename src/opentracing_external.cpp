// Implementation of the exposed makeTracerAndPublisher function.
// This is intentionally kept separate from the makeTracer function, which has additional
// dependencies. It allows the library to be used with an external HTTP implementation for sending
// traces to the Datadog Agent.
//
// See BAZEL.build for the files required to build this library using makeTracerAndPublisher.

#include <datadog/opentracing.h>
#include "tracer.h"
#include "writer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

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
