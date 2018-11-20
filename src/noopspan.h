#ifndef DD_OPENTRACING_NOOPSPAN_H
#define DD_OPENTRACING_NOOPSPAN_H

#include <opentracing/span.h>
#include <opentracing/tracer.h>
#include <memory>
#include "propagation.h"
#include "span.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class Tracer;

// A NoopSpan, provides a noop implementation of opentracing::Span methods
class NoopSpan : public DatadogSpan {
 public:
  // Creates a new NoopSpan, usually called by Tracer::StartSpanWithOptions.
  NoopSpan(std::shared_ptr<const Tracer> tracer, uint64_t span_id, uint64_t trace_id,
           uint64_t parent_id, SpanContext context);
  NoopSpan() = delete;
  NoopSpan(NoopSpan &&other);
  ~NoopSpan() override = default;

  void FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept override;
  void SetOperationName(ot::string_view name) noexcept override;
  void SetTag(ot::string_view key, const ot::Value &value) noexcept override;
  void SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept override;
  std::string BaggageItem(ot::string_view restricted_key) const noexcept override;
  void Log(std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept override;
  const ot::SpanContext &context() const noexcept override;
  const ot::Tracer &tracer() const noexcept override;
  uint64_t traceId() const override;
  uint64_t spanId() const override;
  OptionalSamplingPriority setSamplingPriority(
      std::unique_ptr<UserSamplingPriority> priority) override;
  OptionalSamplingPriority getSamplingPriority() const override;

 private:
  std::shared_ptr<const Tracer> tracer_;
  const uint64_t span_id_;
  const uint64_t trace_id_;
  const uint64_t parent_id_;
  SpanContext context_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_NOOPSPAN_H
