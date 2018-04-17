#include <datadog/opentracing.h>

#include "span.h"
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

uint64_t getId() {
  static thread_local std::mt19937_64 source{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint64_t> distribution;
  return distribution(source);
}

Tracer::Tracer(TracerOptions options)
    : Tracer(options,
             std::shared_ptr<Writer<Span>>(
                 new AgentWriter<Span>{options.agent_host, options.agent_port}),
             getRealTime, getId) {}

Tracer::Tracer(TracerOptions options, std::shared_ptr<Writer<Span>> writer, TimeProvider get_time,
               IdProvider get_id)
    : opts_(options), writer_(std::move(writer)), get_time_(get_time), get_id_(get_id) {}

std::unique_ptr<ot::Span> Tracer::StartSpanWithOptions(ot::string_view operation_name,
                                                       const ot::StartSpanOptions &options) const
    noexcept try {
  return std::move(std::unique_ptr<Span>{new Span{shared_from_this(), writer_, get_time_, get_id_,
                                                  opts_.service, opts_.type, opts_.span_name,
                                                  operation_name, options}});
} catch (const std::bad_alloc &) {
  // At least don't crash.
  return nullptr;
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc, std::ostream &writer) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::TextMapWriter &writer) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::HTTPHeadersWriter &writer) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(std::istream &reader) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::TextMapReader &reader) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::HTTPHeadersReader &reader) const {
  return ot::make_unexpected(ot::invalid_carrier_error);
}

void Tracer::Close() noexcept {}

}  // namespace opentracing
}  // namespace datadog
