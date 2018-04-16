#ifndef DD_OPENTRACING_SPANCONTEXT_H
#define DD_OPENTRACING_SPANCONTEXT_H

#include <opentracing/tracer.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class SpanContext : public ot::SpanContext {
 public:
  SpanContext(uint64_t id, uint64_t trace_id,
              std::unordered_map<std::string, std::string> &&baggage);

  void ForeachBaggageItem(
      std::function<bool(const std::string &, const std::string &)> f) const override;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPANCONTEXT_H
