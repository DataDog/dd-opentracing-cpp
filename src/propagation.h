#ifndef DD_OPENTRACING_PROPAGATION_H
#define DD_OPENTRACING_PROPAGATION_H

#include <opentracing/tracer.h>
#include <mutex>
#include <unordered_map>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

class SpanBuffer;
class SampleProvider;
class SpanData;

enum class SamplingPriority : int {
  UserDrop = -1,
  SamplerDrop = 0,
  SamplerKeep = 1,
  UserKeep = 2,

  MinimumValue = UserDrop,
  MaximumValue = UserKeep,
};

// A SamplingPriority that encompasses only values that may be directly set by users.
enum class UserSamplingPriority : int {
  UserDrop = static_cast<int>(SamplingPriority::UserDrop),
  UserKeep = static_cast<int>(SamplingPriority::UserKeep),
};

// Move to std::optional in C++17 when it has better compiler support.
using OptionalSamplingPriority = std::unique_ptr<SamplingPriority>;

OptionalSamplingPriority asSamplingPriority(int i);

class SpanContext : public ot::SpanContext {
 public:
  SpanContext(uint64_t id, uint64_t trace_id, OptionalSamplingPriority sampling_priority,
              std::unordered_map<std::string, std::string> &&baggage,
              std::shared_ptr<SpanBuffer> pending_traces);

  // Enables a hack, see the comment below on nginx_opentracing_compatibility_hack_.
  static SpanContext NginxOpenTracingCompatibilityHackSpanContext(
      uint64_t id, uint64_t trace_id, OptionalSamplingPriority sampling_priority,
      std::unordered_map<std::string, std::string> &&baggage,
      std::shared_ptr<SpanBuffer> pending_traces);

  SpanContext(SpanContext &&other);
  SpanContext &operator=(SpanContext &&other);

  void ForeachBaggageItem(
      std::function<bool(const std::string &, const std::string &)> f) const override;

  void setBaggageItem(ot::string_view key, ot::string_view value) noexcept;

  std::string baggageItem(ot::string_view key) const;

  // Serializes the context into the given writer.
  ot::expected<void> serialize(std::ostream &writer) const;
  ot::expected<void> serialize(const ot::TextMapWriter &writer) const;

  SpanContext withId(uint64_t id) const;

  // Returns a new context from the given reader.
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::istream &reader, std::shared_ptr<SpanBuffer> pending_traces);
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      const ot::TextMapReader &reader, std::shared_ptr<SpanBuffer> pending_traces);

  uint64_t id() const;
  uint64_t traceId() const;
  OptionalSamplingPriority getSamplingPriority() const;
  void setSamplingPriority(OptionalSamplingPriority p);
  OptionalSamplingPriority assignSamplingPriority(const std::shared_ptr<SampleProvider> &sampler,
                                                  const SpanData *span);

 private:
  // So we don't need a reentrant mutex.
  OptionalSamplingPriority getSamplingPriorityImpl(bool is_root) const;
  void setSamplingPriorityImpl(OptionalSamplingPriority p, bool is_root);

  // Terrible, terrible hack; to get around:
  // https://github.com/opentracing-contrib/nginx-opentracing/blob/master/opentracing/src/discover_span_context_keys.cpp#L49-L50
  // nginx-opentracing needs to know in-advance the headers that may propagate from a tracer. It
  // does this by creating a dummy span, reading the header names from that span, and creating a
  // whitelist from them. This causes a problem, since some headers (eg
  // "x-datadog-sampling-priority") are not sent for every span and therefore aren't added to the
  // whitelist.
  // So we must detect when this dummy span is being asked for, and manually override the
  // serialization of the SpanContext to ensure that every header is present. This bool enables
  // that manual override. The bool is checked in the serialize(...) method, and set when a
  // SpanContext is created not from the constructor but via the static method
  // NginxOpenTracingCompatibilityHackSpanContext.
  // The detection of the dummy span condition happens in tracer.cpp, we look for the operation
  // name "dummySpan".
  // I use a bool and not polymorphism because the move constructor/assignments
  // make it more of a pain to do and less obvious what's happening.
  bool nginx_opentracing_compatibility_hack_ = false;

  uint64_t id_;
  uint64_t trace_id_;
  OptionalSamplingPriority sampling_priority_;
  bool sampling_priority_locked_ = false;
  std::unordered_map<std::string, std::string> baggage_;
  std::shared_ptr<SpanBuffer> pending_traces_;

  mutable std::mutex mutex_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_PROPAGATION_H
