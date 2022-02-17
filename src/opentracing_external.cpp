// Implementation of the exposed makeTracerAndEncoder function.
// This is intentionally kept separate from the makeTracer function, which has additional
// dependencies. It allows the library to be used with an external HTTP implementation for sending
// traces to the Datadog Agent.
//
// See BAZEL.build for the files required to build this library using makeTracerAndEncoder.

#include <datadog/opentracing.h>

#include <sstream>

#include "logger.h"
#include "sample.h"
#include "tracer.h"
#include "tracer_options.h"
#include "writer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>> makeTracerAndEncoder(
    const TracerOptions &options) {
  // Creating the logger here assumes that there are no environment variable
  // dependent settings for the logger, which is true.
  auto logger = makeLogger(options);

  auto maybe_options = applyTracerOptionsFromEnvironment(options);
  if (!maybe_options) {
    // TODO(dgoffredo): Figure out a logging interface that allows for this
    // formatting to be done within the (possibly no-op) logger rather than out
    // here.
    std::ostringstream message;
    message << "Error applying TracerOptions from environment variables: " << maybe_options.error()
            << "\nTracer will be started without options from the environment\n";
    logger->Log(LogLevel::error, message.str());
    maybe_options = options;
  }
  TracerOptions opts = maybe_options.value();

  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::make_shared<ExternalWriter>(sampler, logger);
  auto encoder = writer->encoder();
  return std::tuple<std::shared_ptr<ot::Tracer>, std::shared_ptr<TraceEncoder>>{
      std::shared_ptr<ot::Tracer>{new Tracer{opts, writer, sampler, logger}}, encoder};
}

}  // namespace opentracing
}  // namespace datadog
