#ifndef DD_OPENTRACING_PROPAGATION_H
#define DD_OPENTRACING_PROPAGATION_H

#include <datadog/opentracing.h>
#include <opentracing/tracer.h>
#include <mutex>
#include <set>
#include <unordered_map>
#include "logger.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Header name prefix for OpenTracing baggage. Should be "ot-baggage-" to support OpenTracing
// interop.
const ot::string_view baggage_prefix = "ot-baggage-";

// Returns a list of strings, where each string is a header that will be used for propagating
// traces.
std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles,
                                                       bool prioritySamplingEnabled);

class Tracer;
class SpanBuffer;
struct HeadersImpl;

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
  SpanContext(std::shared_ptr<const Logger> logger, uint64_t id, uint64_t trace_id,
              std::string origin, std::unordered_map<std::string, std::string> &&baggage);

  // Enables a hack, see the comment below on nginx_opentracing_compatibility_hack_.
  static SpanContext NginxOpenTracingCompatibilityHackSpanContext(
      std::shared_ptr<const Logger> logger, uint64_t id, uint64_t trace_id,
      std::unordered_map<std::string, std::string> &&baggage);

  SpanContext(SpanContext &&other);
  SpanContext &operator=(SpanContext &&other);
  bool operator==(const SpanContext &other) const;
  bool operator!=(const SpanContext &other) const;

  void ForeachBaggageItem(
      std::function<bool(const std::string &, const std::string &)> f) const override;

  void setBaggageItem(ot::string_view key, ot::string_view value) noexcept;

  std::string baggageItem(ot::string_view key) const;

  // Serializes the context into the given writer.
  ot::expected<void> serialize(std::ostream &writer,
                               const std::shared_ptr<SpanBuffer> pending_traces,
                               bool prioritySamplingEnabled) const;
  ot::expected<void> serialize(const ot::TextMapWriter &writer,
                               const std::shared_ptr<SpanBuffer> pending_traces,
                               std::set<PropagationStyle> styles,
                               bool prioritySamplingEnabled) const;

  SpanContext withId(uint64_t id) const;

  // Returns a new context from the given reader.
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> tracer, std::istream &reader);
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> tracer, const ot::TextMapReader &reader,
      std::set<PropagationStyle> styles);

  uint64_t id() const;
  uint64_t traceId() const;
  // Returns an OptionalSamplingPriority, the propagated sampling priority. It may hold a value of
  // nullptr, in which case either there has been no propagation or the up-stream tracer did not
  // set a sampling priority.
  OptionalSamplingPriority getPropagatedSamplingPriority() const;
  // Returns the propagated "origin". It returns an empty string if no origin was provided.
  const std::string origin() const;

 private:
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> tracer, const ot::TextMapReader &reader,
      const HeadersImpl &headers_impl);
  ot::expected<void> serialize(const ot::TextMapWriter &writer,
                               const std::shared_ptr<SpanBuffer> pending_traces,
                               const HeadersImpl &headers_impl,
                               bool prioritySamplingEnabled) const;

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

  std::shared_ptr<const Logger> logger_;
  uint64_t id_;
  uint64_t trace_id_;
  OptionalSamplingPriority propagated_sampling_priority_ = nullptr;
  std::string origin_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> baggage_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_PROPAGATION_H
