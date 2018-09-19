// Implementation of the exposed makeTracer function.
// This is kept separately to isolate the AgentWriter and its cURL dependency.
// Users of the library that do not use this tracer are able to avoid the
// additional dependency and implementation details.

#include <datadog/opentracing.h>
#include "agent_writer.h"
#include "sample.h"
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  std::shared_ptr<SampleProvider> sampler = sampleProviderFromOptions(options);
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(options.agent_host, options.agent_port,
                      std::chrono::milliseconds(llabs(options.write_period_ms)), sampler)};
  return std::shared_ptr<ot::Tracer>{new Tracer{options, writer, sampler}};
}

}  // namespace opentracing
}  // namespace datadog
