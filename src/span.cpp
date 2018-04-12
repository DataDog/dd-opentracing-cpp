#include "span.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

Span::Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<Recorder> recorder,
           TimeProvider get_time, IdProvider next_id, std::string span_service,
           std::string span_type, std::string span_name, ot::string_view resource,
           const ot::StartSpanOptions &options)
    : tracer_(std::move(tracer)),
      get_time_(get_time),
      recorder_(std::move(recorder)),
      start_time_(get_time_()),
      name(span_name),
      resource(resource),
      service(span_service),
      type(span_type),
      span_id(next_id()),
      trace_id(span_id),
      parent_id(0),
      error(0),
      start(std::chrono::duration_cast<std::chrono::nanoseconds>(
                start_time_.absolute_time.time_since_epoch())
                .count()),
      duration(0),
      context_(span_id, span_id, {}) {}

Span::~Span() noexcept {}

void Span::FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept try {
  auto end_time = get_time_();
  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
  recorder_->RecordSpan(std::move(*this));
} catch (const std::bad_alloc &) {
  // At least don't crash.
}

void Span::SetOperationName(ot::string_view name) noexcept {}

void Span::SetTag(ot::string_view key, const ot::Value &value) noexcept {}

void Span::SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept {}

std::string Span::BaggageItem(ot::string_view restricted_key) const noexcept { return ""; }

void Span::Log(std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept {}

const ot::SpanContext &Span::context() const noexcept { return context_; }

const ot::Tracer &Span::tracer() const noexcept { return *tracer_; }

}  // namespace opentracing
}  // namespace datadog
