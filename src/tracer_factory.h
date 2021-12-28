#ifndef DD_OPENTRACING_TRACER_FACTORY_H
#define DD_OPENTRACING_TRACER_FACTORY_H

#include <datadog/opentracing.h>
#include <opentracing/tracer_factory.h>

#include "agent_writer.h"
#include "tracer.h"
#include "tracer_options.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

ot::expected<TracerOptions> optionsFromConfig(const char *configuration,
                                              std::string &error_message);

template <class TracerImpl>
class TracerFactory : public ot::TracerFactory {
 public:
  // Accepts configuration in JSON format, with the following keys:
  // "service": Required. A string, the name of the service.
  // "agent_host": A string, defaults to localhost. Can also be set by the environment variable
  //     DD_AGENT_HOST
  // "agent_port": A number, defaults to 8126. "type": A string, defaults to web. Can also be set
  //     by the environment variable DD_TRACE_AGENT_PORT
  // "type": A string, defaults to web.
  // "environment": A string, defaults to "". The environment this trace belongs to.
  //     eg. "" (env:none), "staging", "prod". Can also be set by the environment variable
  //     DD_ENV
  // "sample_rate": A double, defaults to 1.0.
  // "operation_name_override": A string, if not empty it overrides the operation name (and the
  //     overridden operation name is recorded in the tag "operation").
  // "propagation_style_extract": A list of strings, each string is one of "Datadog", "B3".
  //     Defaults to ["Datadog"]. The type of headers to use to propagate
  //     distributed traces. Can also be set by the environment variable
  //     DD_PROPAGATION_STYLE_EXTRACT.
  // "propagation_style_inject": A list of strings, each string is one of "Datadog", "B3". Defaults
  //     to ["Datadog"]. The type of headers to use to receive distributed traces. Can also be set
  //     by the environment variable DD_PROPAGATION_STYLE_INJECT.
  //
  // Extra keys will be ignored.
  ot::expected<std::shared_ptr<ot::Tracer>> MakeTracer(const char *configuration,
                                                       std::string &error_message) const
      noexcept override;
};

template <class TracerImpl>
ot::expected<std::shared_ptr<ot::Tracer>> TracerFactory<TracerImpl>::MakeTracer(
    const char *configuration, std::string &error_message) const noexcept try {
  auto maybe_options = optionsFromConfig(configuration, error_message);
  if (!maybe_options) {
    return ot::make_unexpected(maybe_options.error());
  }
  TracerOptions options = maybe_options.value();

  auto logger = makeLogger(options);
  auto sampler = std::make_shared<RulesSampler>();
  auto writer = std::shared_ptr<Writer>{
      new AgentWriter(options.agent_host, options.agent_port, options.agent_url,
                      std::chrono::milliseconds(llabs(options.write_period_ms)), sampler, logger)};

  return std::shared_ptr<ot::Tracer>{new TracerImpl{options, writer, sampler, logger}};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
} catch (const std::runtime_error &e) {
  error_message = e.what();
  return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
}

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TRACER_FACTORY_H
