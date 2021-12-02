// Implementation of the exposed makeTracerAndEncoder function.
// This is intentionally kept separate from the makeTracer function, which has additional
// dependencies. It allows the library to be used with an external HTTP implementation for sending
// traces to the Datadog Agent.
//
// See BAZEL.build for the files required to build this library using makeTracerAndEncoder.

#include <datadog/opentracing.h>

#include "logger.h"
#include "sample.h"
#include "tracer.h"
#include "tracer_options.h"
#include "writer.h"

#include <sstream>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>> makeTracerAndEncoder(
    const TracerOptions &options) {
  auto maybe_options = applyTracerOptionsFromEnvironment(options);
  if (!maybe_options) {
    std::ostringstream message;
    message << "Error applying TracerOptions from environment variables: "
              << maybe_options.error()
              << "\nTracer will be started without options from the environment\n";
    StandardLogger(options.log_func).Log(LogLevel::error, message.str());
    maybe_options = options;
  }
  TracerOptions opts = maybe_options.value();

  auto logger = makeLogger(opts);
  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::make_shared<ExternalWriter>(sampler, logger);
  auto encoder = writer->encoder();
  return std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>>{
      std::shared_ptr<ot::Tracer>{new Tracer{opts, writer, sampler, logger}}, encoder};
}

}  // namespace opentracing
}  // namespace datadog
