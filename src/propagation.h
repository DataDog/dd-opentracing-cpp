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
std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles);

class Tracer;
class Writer;
struct HeadersImpl;
class RulesSampler;
class ActiveTrace;
struct SamplingStatus;

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

class SpanContext : public ot::SpanContext {
 public:
  SpanContext(std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
              std::shared_ptr<ActiveTrace> active_trace, uint64_t id, uint64_t trace_id,
              std::string origin, std::unordered_map<std::string, std::string> &&baggage);
  SpanContext(std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
              std::shared_ptr<ActiveTrace> active_trace, uint64_t id, uint64_t trace_id);

  // Enables a hack, see the comment below on nginx_opentracing_compatibility_hack_.
  static SpanContext NginxOpenTracingCompatibilityHackSpanContext(
      std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler, uint64_t id,
      uint64_t trace_id);
  SpanContext(const SpanContext &other);
  SpanContext(SpanContext &&other);
  SpanContext &operator=(const SpanContext &other);
  SpanContext &operator=(SpanContext &&other);
  bool operator==(const SpanContext &other) const;
  bool operator!=(const SpanContext &other) const;
  void ForeachBaggageItem(
      std::function<bool(const std::string &, const std::string &)> f) const override;

  std::unique_ptr<ot::SpanContext> Clone() const noexcept override;
  std::string ToTraceID() const noexcept override;
  std::string ToSpanID() const noexcept override;

  void setBaggageItem(ot::string_view key, ot::string_view value) noexcept;

  std::string baggageItem(ot::string_view key) const;

  // Serializes the context into the given writer.
  ot::expected<void> serialize(std::ostream &writer) const;
  ot::expected<void> serialize(const ot::TextMapWriter &writer,
                               std::set<PropagationStyle> styles) const;

  // Returns a new context from the given reader.
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
      std::shared_ptr<Writer> writer, std::istream &reader);
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
      std::shared_ptr<Writer> writer, const ot::TextMapReader &reader,
      std::set<PropagationStyle> styles);

  uint64_t id() const;
  uint64_t traceId() const;
  SamplingStatus samplingStatus() const;
  // Returns the propagated "origin". It returns an empty string if no origin was provided.
  const std::string origin() const;
  SpanContext childContext(uint64_t id) const;
  std::shared_ptr<ActiveTrace> activeTrace() const;
  void setEnv(std::string env);
  void setService(std::string service);
  void setName(std::string name);
  void sample();
  bool topLevel() const;
  bool extracted() const;

 private:
  static ot::expected<std::unique_ptr<ot::SpanContext>> deserialize(
      std::shared_ptr<const Logger> tracer, std::shared_ptr<RulesSampler> sampler,
      std::shared_ptr<Writer> writer, const ot::TextMapReader &reader,
      const HeadersImpl &headers_impl);
  ot::expected<void> serialize(const ot::TextMapWriter &writer,
                               const HeadersImpl &headers_impl) const;

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
  std::shared_ptr<RulesSampler> sampler_;
  std::shared_ptr<ActiveTrace> active_trace_;
  uint64_t id_ = 0;
  uint64_t trace_id_ = 0;
  std::string origin_;

  bool is_toplevel_ = false;
  bool is_extracted_ = false;

  mutable std::mutex mutex_;
  std::string env_;
  std::string service_;
  std::string name_;

  std::unordered_map<std::string, std::string> baggage_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_PROPAGATION_H
