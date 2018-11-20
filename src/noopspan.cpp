#include "noopspan.h"
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

NoopSpan::NoopSpan(std::shared_ptr<const Tracer> tracer, uint64_t span_id, uint64_t trace_id,
                   uint64_t parent_id, SpanContext context)
    : tracer_(std::move(tracer)),
      span_id_(span_id),
      trace_id_(trace_id),
      parent_id_(parent_id),
      context_(std::move(context)) {}

NoopSpan::NoopSpan(NoopSpan &&other)
    : tracer_(other.tracer_),
      span_id_(other.span_id_),
      trace_id_(other.trace_id_),
      parent_id_(other.parent_id_),
      context_(std::move(other.context_)) {}

void NoopSpan::FinishWithOptions(
    const ot::FinishSpanOptions & /* finish_span_options */) noexcept {}

void NoopSpan::SetOperationName(ot::string_view /* operation_name */) noexcept {}

void NoopSpan::SetTag(ot::string_view /* key */, const ot::Value & /* value */) noexcept {}

void NoopSpan::SetBaggageItem(ot::string_view /* restricted_key */,
                              ot::string_view /* value */) noexcept {}

std::string NoopSpan::BaggageItem(ot::string_view restricted_key) const noexcept {
  return context_.baggageItem(restricted_key);
}

void NoopSpan::Log(
    std::initializer_list<std::pair<ot::string_view, ot::Value>> /* fields */) noexcept {}

const ot::SpanContext &NoopSpan::context() const noexcept { return context_; }

const ot::Tracer &NoopSpan::tracer() const noexcept { return *tracer_; }

uint64_t NoopSpan::traceId() const { return trace_id_; }
uint64_t NoopSpan::spanId() const { return span_id_; }

OptionalSamplingPriority NoopSpan::setSamplingPriority(
    std::unique_ptr<UserSamplingPriority> /* priority */) {
  return nullptr;
};
OptionalSamplingPriority NoopSpan::getSamplingPriority() const { return nullptr; };

}  // namespace opentracing
}  // namespace datadog
