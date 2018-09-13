#ifndef DD_OPENTRACING_PROPAGATION_H
#define DD_OPENTRACING_PROPAGATION_H

#include <opentracing/tracer.h>
#include <mutex>
#include <unordered_map>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

enum class SamplingPriority : int {
  UserDrop = -1,
  SamplerDrop = 0,
  SamplerKeep = 1,
  UserKeep = 2,
};

// Move to std::optional in C++17 when it has better compiler support.
using OptionalSamplingPriority = std::unique_ptr<SamplingPriority>;

OptionalSamplingPriority asSamplingPriority(int i);

class SpanContext : public ot::SpanContext {
 public:
  SpanContext(uint64_t id, uint64_t trace_id, OptionalSamplingPriority sampling_priority,
              std::unordered_map<std::string, std::string> &&baggage);

  SpanContext(SpanContext &&other);
  SpanContext &operator=(SpanContext &&other);

  void ForeachBaggageItem(
      std::function<bool(const std::string &, const std::string &)> f) const override;

  void setBaggageItem(ot::string_view key, ot::string_view value) noexcept;

  std::string baggageItem(ot::string_view key) const;

  // Serializes the context into the given writer.
  ot::expected<void> serialize(const ot::TextMapWriter &writer) const;

  SpanContext withId(uint64_t id) const;

  // Returns a new context from the given reader.
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      const ot::TextMapReader &reader);

  uint64_t id() const;
  uint64_t trace_id() const;
  OptionalSamplingPriority getSamplingPriority() const;
  void setSamplingPriority(OptionalSamplingPriority p);

 private:
  uint64_t id_;
  uint64_t trace_id_;
  OptionalSamplingPriority sampling_priority_;
  std::unordered_map<std::string, std::string> baggage_;
  mutable std::mutex mutex_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_PROPAGATION_H
