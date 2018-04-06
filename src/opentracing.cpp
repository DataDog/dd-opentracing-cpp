#include <datadog/opentracing.h>

namespace ot = opentracing;

namespace datadog {

class Tracer : public ot::Tracer {
 public:
  std::unique_ptr<ot::Span> StartSpanWithOptions(ot::string_view operation_name,
                                                 const ot::StartSpanOptions &options) const
      noexcept override {
    return {};
  };

  ot::expected<void> Inject(const ot::SpanContext &sc, std::ostream &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::TextMapWriter &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::HTTPHeadersWriter &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(std::istream &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::TextMapReader &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::HTTPHeadersReader &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  };

  void Close() noexcept override{};
};

std::shared_ptr<ot::Tracer> makeTracer(const TracerOptions &options) {
  return std::shared_ptr<ot::Tracer>{new Tracer{}};
}

}  // namespace datadog