#ifndef DD_OPENTRACING_TRACER_H
#define DD_OPENTRACING_TRACER_H

#include <datadog/opentracing.h>
#include <functional>
#include <random>
#include "clock.h"
#include "encoder.h"
#include "sample.h"
#include "span.h"
#include "span_buffer.h"
#include "writer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class SpanBuffer;

// The interface for providing IDs to spans and traces.
typedef std::function<uint64_t()> IdProvider;

uint64_t getId();

class Tracer : public ot::Tracer, public std::enable_shared_from_this<Tracer> {
 public:
  // Creates a Tracer by copying the given options and injecting the given dependencies.
  Tracer(TracerOptions options, std::shared_ptr<SpanBuffer> buffer, TimeProvider get_time,
         IdProvider get_id, std::shared_ptr<SampleProvider> sampler);

  // Creates a Tracer by copying the given options and using the preconfigured writer.
  // The writer is either an AgentWriter that sends trace data directly to the Datadog Agent, or
  // an ExternalWriter that requires an external HTTP client to encode and submit to the Datadog
  // Agent.
  Tracer(TracerOptions options, std::shared_ptr<Writer> &writer,
         std::shared_ptr<SampleProvider> sampler);

  Tracer() = delete;

  // Starts a new span.
  std::unique_ptr<ot::Span> StartSpanWithOptions(ot::string_view operation_name,
                                                 const ot::StartSpanOptions &options) const
      noexcept override;

  ot::expected<void> Inject(const ot::SpanContext &sc, std::ostream &writer) const override;

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::TextMapWriter &writer) const override;

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::HTTPHeadersWriter &writer) const override;

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(std::istream &reader) const override;

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::TextMapReader &reader) const override;

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::HTTPHeadersReader &reader) const override;

  void Close() noexcept override;

 private:
  template <class Writer>
  ot::expected<void> inject(const ot::SpanContext &sc, Writer &writer) const try {
    auto span_context = dynamic_cast<const SpanContext *>(&sc);
    if (span_context == nullptr) {
      return ot::make_unexpected(ot::invalid_span_context_error);
    }
    return span_context->serialize(writer);
  } catch (const std::bad_alloc &) {
    return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
  }

  const TracerOptions opts_;
  // Keeps finished spans until their entire trace is finished.
  std::shared_ptr<SpanBuffer> buffer_;
  TimeProvider get_time_;
  IdProvider get_id_;
  std::shared_ptr<SampleProvider> sampler_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TRACER_H
