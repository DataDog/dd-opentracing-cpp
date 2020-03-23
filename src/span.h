#ifndef DD_OPENTRACING_SPAN_H
#define DD_OPENTRACING_SPAN_H

#include <msgpack.hpp>
#include "clock.h"
#include "propagation.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class Tracer;
class SpanBuffer;
typedef std::function<uint64_t()> IdProvider;  // See tracer.h

// Contains data that describes a Span.
struct SpanData {
  ~SpanData() = default;

  friend std::unique_ptr<SpanData> makeSpanData(std::string type, std::string service,
                                                ot::string_view resource, std::string name,
                                                uint64_t trace_id, uint64_t span_id,
                                                uint64_t parent_id, int64_t start);

  friend std::unique_ptr<SpanData> stubSpanData();

 protected:  // Can only be created in a unique_ptr (or in a subclassed test class).
  SpanData(std::string type, std::string service, ot::string_view resource, std::string name,
           uint64_t trace_id, uint64_t span_id, uint64_t parent_id, int64_t start,
           int64_t duration, int32_t error);
  SpanData();
  SpanData(const SpanData &) = default;
  SpanData &operator=(const SpanData &) = delete;
  SpanData(const SpanData &&) = delete;
  SpanData &operator=(const SpanData &&) = delete;

 public:
  std::string type;
  std::string service;
  std::string resource;
  std::string name;
  uint64_t trace_id;
  uint64_t span_id;
  uint64_t parent_id;
  int64_t start;
  int64_t duration;
  int32_t error;
  std::unordered_map<std::string, std::string> meta;  // Aka, tags.
  std::unordered_map<std::string, double> metrics;

  uint64_t traceId() const;
  uint64_t spanId() const;
  const std::string env() const;

  MSGPACK_DEFINE_MAP(name, service, resource, type, start, duration, meta, metrics, span_id,
                     trace_id, parent_id, error)
};

// A common interface for Datadog-specific Span operations.
class DatadogSpan : public ot::Span {
 public:
  // ot::Span methods.
  virtual void FinishWithOptions(
      const ot::FinishSpanOptions &finish_span_options) noexcept override = 0;
  virtual void SetOperationName(ot::string_view name) noexcept override = 0;
  virtual void SetTag(ot::string_view key, const ot::Value &value) noexcept override = 0;
  virtual void SetBaggageItem(ot::string_view restricted_key,
                              ot::string_view value) noexcept override = 0;
  virtual std::string BaggageItem(ot::string_view restricted_key) const noexcept override = 0;
  virtual void Log(
      std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept override = 0;
  virtual const ot::SpanContext &context() const noexcept override = 0;
  virtual const ot::Tracer &tracer() const noexcept override = 0;

  // Datadog methods.

  // Sets the SamplingPriority. If priority is null, then unsets SamplingPriority. Returns the
  // value of the SamplingPriority; this may not be the same as the given parameter if this trace
  // has propagated from a remote origin and already has a SamplingPriority.
  virtual OptionalSamplingPriority setSamplingPriority(
      std::unique_ptr<UserSamplingPriority> priority) = 0;
  virtual OptionalSamplingPriority getSamplingPriority() const = 0;
  virtual uint64_t traceId() const = 0;
  virtual uint64_t spanId() const = 0;
};

// A Span, a component of a trace, a single instrumented event.
class Span : public DatadogSpan {
 public:
  // Creates a new Span.
  Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<SpanBuffer> buffer,
       TimeProvider get_time, uint64_t span_id, uint64_t trace_id, uint64_t parent_id,
       SpanContext context, TimePoint start_time, std::string span_service, std::string span_type,
       std::string span_name, std::string resource, std::string operation_name_override);

  Span() = delete;
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

  uint64_t traceId() const override;
  uint64_t spanId() const override;
  OptionalSamplingPriority setSamplingPriority(
      std::unique_ptr<UserSamplingPriority> priority) override;
  OptionalSamplingPriority getSamplingPriority() const override;

 private:
  OptionalSamplingPriority assignSamplingPriority()
      const;  // Sooo not const. See definition of method Span::context.

  mutable std::mutex mutex_;
  std::atomic<bool> is_finished_{false};

  // Set in constructor initializer:
  std::shared_ptr<const Tracer> tracer_;
  std::shared_ptr<SpanBuffer> buffer_;
  TimeProvider get_time_;
  SpanContext context_;
  TimePoint start_time_;
  std::string operation_name_override_;

  // Set in constructor initializer, depends on previous constructor initializer-set members:
  std::unique_ptr<SpanData> span_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_H
