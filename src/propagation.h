#ifndef DD_OPENTRACING_PROPAGATION_H
#define DD_OPENTRACING_PROPAGATION_H

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

  ot::expected<void> serialize(const ot::TextMapWriter &writer) const;

  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      const ot::TextMapReader &reader);

  uint64_t id() const;
  uint64_t trace_id() const;

 private:
  uint64_t id_;
  uint64_t trace_id_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_PROPAGATION_H
