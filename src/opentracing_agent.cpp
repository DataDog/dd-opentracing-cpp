// Implementation of the exposed makeTracer function.
// This is kept separately to isolate the AgentWriter and its cURL dependency.
// Users of the library that do not use this tracer are able to avoid the
// additional dependency and implementation details.

#include <datadog/opentracing.h>

#include "agent_writer.h"
#include "logger.h"
#include "sample.h"
#include "tracer.h"
#include "tracer_options.h"

#include <sstream>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
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
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(opts.agent_host, opts.agent_port, opts.agent_url,
                      std::chrono::milliseconds(llabs(opts.write_period_ms)), sampler, logger)};
  return std::shared_ptr<ot::Tracer>{new Tracer{opts, writer, sampler, logger}};
}

}  // namespace opentracing
}  // namespace datadog
