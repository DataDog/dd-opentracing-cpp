#ifndef DD_OPENTRACING_TRACER_FACTORY_H
#define DD_OPENTRACING_TRACER_FACTORY_H

#include <datadog/opentracing.h>
#include <opentracing/tracer_factory.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

ot::expected<TracerOptions> optionsFromConfig(const char *configuration,
                                              std::string &error_message);

template <class TracerImpl>
class TracerFactory : public ot::TracerFactory {
 public:
  ot::expected<std::shared_ptr<ot::Tracer>> MakeTracer(const char *configuration,
                                                       std::string &error_message) const
      noexcept override;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TRACER_FACTORY_H
