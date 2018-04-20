#ifndef DD_OPENTRACING_TRACER_H
#define DD_OPENTRACING_TRACER_H

#include <datadog/opentracing.h>
#include "clock.h"
#include "span.h"
#include "writer.h"

#include <functional>
#include <random>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

template <class MsgType>
class Writer;
class Span;

// The interface for providing IDs to spans and traces.
typedef std::function<uint64_t()> IdProvider;

uint64_t getId();

class Tracer : public ot::Tracer, public std::enable_shared_from_this<Tracer> {
 public:
  // Creates a Tracer by copying the given options.
  Tracer(TracerOptions options);

  // Creates a Tracer by copying the given options and injecting the given dependencies.
  Tracer(TracerOptions options, std::shared_ptr<Writer<Span>> writer, TimeProvider get_time,
         IdProvider get_id);

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
  ot::expected<void> inject(const ot::SpanContext &sc, const ot::TextMapWriter &writer) const;

  const TracerOptions opts_;
  std::shared_ptr<Writer<Span>> writer_;  // Records spans (eg sends to the agent).
  TimeProvider get_time_;
  IdProvider get_id_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TRACER_H
