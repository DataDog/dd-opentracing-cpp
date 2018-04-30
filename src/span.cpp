#include "span.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

Span::Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<Writer<Span>> writer,
           TimeProvider get_time, IdProvider next_id, std::string span_service,
           std::string span_type, std::string span_name, ot::string_view resource,
           const ot::StartSpanOptions &options)
    : tracer_(std::move(tracer)),
      get_time_(get_time),
      writer_(std::move(writer)),
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
      context_(span_id, span_id, {}) {
  // Extract context (if present) from options.
  // TODO[willgittoes-dd]: Consider making all this logic happen in the initializer list, so we can
  // make the ID members const.
  const SpanContext *parent_span_context = nullptr;
  for (auto &reference : options.references) {
    if (auto span_context = dynamic_cast<const SpanContext *>(reference.second)) {
      parent_span_context = span_context;
      break;
    }
  }
  if (parent_span_context != nullptr) {
    trace_id = parent_span_context->trace_id();
    parent_id = parent_span_context->id();
    context_ = parent_span_context->withId(span_id);
  }
}

Span::Span(Span &&other)
    : tracer_(other.tracer_),
      get_time_(other.get_time_),
      writer_(other.writer_),
      start_time_(other.start_time_),
      name(other.name),
      service(other.service),
      resource(other.resource),
      type(other.type),
      span_id(other.span_id),
      trace_id(other.trace_id),
      parent_id(other.parent_id),
      error(other.error),
      start(other.start),
      duration(other.duration),
      context_(std::move(other.context_)) {
  is_finished_ = (bool)other.is_finished_;  // Copy the value.
}

Span::~Span() {
  if (!is_finished_) {
    this->Finish();
  }
}

void Span::FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept try {
  if (is_finished_.exchange(true)) {
    return;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  auto end_time = get_time_();
  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
  writer_->write(std::move(*this));
} catch (const std::bad_alloc &) {
  // At least don't crash.
}

void Span::SetOperationName(ot::string_view name) noexcept {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  name = name;
}

void Span::SetTag(ot::string_view key, const ot::Value &value) noexcept {}

void Span::SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept {
  context_.setBaggageItem(restricted_key, value);
}

std::string Span::BaggageItem(ot::string_view restricted_key) const noexcept {
  return context_.baggageItem(restricted_key);
}

void Span::Log(std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept {}

const ot::SpanContext &Span::context() const noexcept { return context_; }

const ot::Tracer &Span::tracer() const noexcept { return *tracer_; }

uint64_t Span::traceId() const {
  return trace_id;  // Never modified, hence un-locked access.
}

}  // namespace opentracing
}  // namespace datadog
