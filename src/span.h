#ifndef DD_OPENTRACING_SPAN_H
#define DD_OPENTRACING_SPAN_H

#include <msgpack.hpp>
#include "propagation.h"
#include "tracer.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class Tracer;
template <class MsgType>
class Writer;
typedef std::function<uint64_t()> IdProvider;  // See tracer.h

// A Span, a component of a trace, a single instrumented event.
class Span : public ot::Span {
 public:
  // Creates a new Span, usually called by Tracer::CreateSpanFromOptions.
  Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<Writer<Span>> writer,
       TimeProvider get_time, IdProvider next_id, std::string span_service, std::string span_type,
       std::string span_name, ot::string_view resource, const ot::StartSpanOptions &options);

  Span() = delete;
  Span(Span &&other);
  ~Span() override;

  // Finishes and records the span.
  void FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept override;

  void SetOperationName(ot::string_view name) noexcept override;

  void SetTag(ot::string_view key, const ot::Value &value) noexcept override;

  void SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept override;

  std::string BaggageItem(ot::string_view restricted_key) const noexcept override;

  void Log(std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept override;

  const ot::SpanContext &context() const noexcept override;

  const ot::Tracer &tracer() const noexcept override;

 private:
  std::shared_ptr<const Tracer> tracer_;
  TimeProvider get_time_;
  std::shared_ptr<Writer<Span>> writer_;
  TimePoint start_time_;
  std::atomic<bool> is_finished_{false};
  std::mutex mutex_;

  // An exception to the naming convention is made here because the variable names themselves are
  // used by msgpack as dictionary keys.
  std::string name;
  std::string service;
  std::string resource;
  std::string type;
  uint64_t span_id;
  uint64_t trace_id;
  uint64_t parent_id;
  int32_t error;
  int64_t start;
  int64_t duration;

  SpanContext context_;

 public:
  MSGPACK_DEFINE_MAP(name, service, resource, type, start, duration, span_id, trace_id, parent_id,
                     error);
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_H
