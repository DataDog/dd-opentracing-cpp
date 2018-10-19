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

  // Ensure we aren't hiding methods from ot::Tracer due to the overloaded overrides.
  using ot::Tracer::Extract;
  using ot::Tracer::Inject;

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
