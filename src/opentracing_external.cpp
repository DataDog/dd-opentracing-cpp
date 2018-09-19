// Implementation of the exposed makeTracerAndEncoder function.
// This is intentionally kept separate from the makeTracer function, which has additional
// dependencies. It allows the library to be used with an external HTTP implementation for sending
// traces to the Datadog Agent.
//
// See BAZEL.build for the files required to build this library using makeTracerAndEncoder.

#include <datadog/opentracing.h>
#include "sample.h"
#include "tracer.h"
#include "writer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>> makeTracerAndEncoder(
    const TracerOptions &options) {
  std::shared_ptr<SampleProvider> sampler = sampleProviderFromOptions(options);
  auto xwriter = std::make_shared<ExternalWriter>(sampler);
  auto encoder = xwriter->encoder();
  std::shared_ptr<Writer> writer = xwriter;
  return std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>>{
      std::shared_ptr<ot::Tracer>{new Tracer{options, writer, sampler}}, encoder};
}

}  // namespace opentracing
}  // namespace datadog
