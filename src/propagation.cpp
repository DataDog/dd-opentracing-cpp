#include "propagation.h"

#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#include "sample.h"
#include "span_buffer.h"

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

struct HeadersImpl {
  const char *trace_id_header;
  const char *span_id_header;
  const char *sampling_priority_header;
  const char *origin_header;
  const int base;
  std::string (*encode_id)(uint64_t);
  std::string (*encode_sampling_priority)(SamplingPriority);
};

namespace {
std::string asHex(uint64_t id) {
  std::stringstream stream;
  stream << std::hex << id;
  return stream.str();
}

// B3 style header propagation only supports "drop" and "keep", with no distinction between
// user/sampler as the decision maker. Here we clamp the serialized values.
std::string clampB3SamplingPriorityValue(SamplingPriority p) {
  if (static_cast<int>(p) > 0) {
    return "1";  // Keep, as SamplingPriority::SamplerKeep.
  }
  return "0";  // Drop, as SamplingPriority::SamplerDrop.
}

std::string to_string(SamplingPriority p) { return std::to_string(static_cast<int>(p)); }

// Header names for trace data. Hax constexpr map-like object.
constexpr struct {
  // https://docs.datadoghq.com/tracing/faq/distributed-tracing/
  HeadersImpl datadog{"x-datadog-trace-id",
                      "x-datadog-parent-id",
                      "x-datadog-sampling-priority",
                      "x-datadog-origin",
                      10,
                      std::to_string,
                      to_string};
  // https://github.com/openzipkin/b3-propagation
  HeadersImpl b3{"X-B3-TraceId",
                 "X-B3-SpanId",
                 "X-B3-Sampled",
                 "x-datadog-origin",
                 16,
                 asHex,
                 clampB3SamplingPriorityValue};

  const HeadersImpl &operator[](const PropagationStyle style) const {
    if (style == PropagationStyle::B3) {
      return b3;
    }
    return datadog;
  };

} propagation_headers;

// Key names for binary serialization in JSON
const std::string json_trace_id_key = "trace_id";
const std::string json_parent_id_key = "parent_id";
const std::string json_sampling_priority_key = "sampling_priority";
const std::string json_origin_key = "origin";
const std::string json_baggage_key = "baggage";

// Does what it says on the tin. Just looks at each char, so don't try and use this on
// unicode strings, only used for comparing HTTP header names.
// Rolled my own because I don't want to import all of libboost for a couple of functions!
bool equals_ignore_case(const std::string &a, const std::string &b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](char a, char b) { return tolower(a) == tolower(b); });
}

// Checks to see if the given string has the given prefix.
bool has_prefix(const std::string &str, const std::string &prefix) {
  if (str.size() < prefix.size()) {
    return false;
  }
  auto result = std::mismatch(prefix.begin(), prefix.end(), str.begin());
  return result.first == prefix.end();
}

// If the result of `SpanContext::deserialize` can be determined solely from
// the presence of certain tags, return the appropriate result.  If the result
// cannot be determined, return `nullptr`.  Each specified boolean indicates
// whether the corresponding tag is set.  Note that `std::unique_ptr` is here
// used as a substitute for `std::optional`.
std::unique_ptr<ot::expected<std::unique_ptr<ot::SpanContext>>> enforce_tag_presence_policy(
    bool trace_id_set, bool parent_id_set, bool origin_set) {
  using Result = ot::expected<std::unique_ptr<ot::SpanContext>>;

  if (!trace_id_set && !parent_id_set) {
    // Both IDs are empty; return an empty context.
    return std::make_unique<Result>();
  }
  if (!trace_id_set) {
    // There's a parent ID without a trace ID.
    return std::make_unique<Result>(ot::make_unexpected(ot::span_context_corrupted_error));
  }
  if (!parent_id_set && !origin_set) {
    // Parent ID is required, except when origin is set.
    return std::make_unique<Result>(ot::make_unexpected(ot::span_context_corrupted_error));
  }
  return nullptr;
}

}  // namespace

std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles) {
  std::vector<ot::string_view> headers;
  for (auto &style : styles) {
    headers.push_back(propagation_headers[style].trace_id_header);
    headers.push_back(propagation_headers[style].span_id_header);
    headers.push_back(propagation_headers[style].sampling_priority_header);
    headers.push_back(propagation_headers[style].origin_header);
  }
  return headers;
}

bool validSamplingPriority(int i) {
  if (i >= static_cast<int>(SamplingPriority::MinimumValue) &&
      i <= static_cast<int>(SamplingPriority::MaximumValue)) {
    return true;
  }
  return false;
}

SpanContext::SpanContext(std::shared_ptr<const Logger> logger,
                         std::shared_ptr<RulesSampler> sampler,
                         std::shared_ptr<ActiveTrace> active_trace, uint64_t id, uint64_t trace_id,
                         std::string origin,
                         std::unordered_map<std::string, std::string> &&baggage)
    : logger_(std::move(logger)),
      sampler_(sampler),
      active_trace_(active_trace),
      id_(id),
      trace_id_(trace_id),
      origin_(origin),
      is_toplevel_(id == trace_id),
      baggage_(std::move(baggage)) {}

SpanContext::SpanContext(std::shared_ptr<const Logger> logger,
                         std::shared_ptr<RulesSampler> sampler,
                         std::shared_ptr<ActiveTrace> active_trace, uint64_t id, uint64_t trace_id)
    : logger_(std::move(logger)),
      sampler_(sampler),
      active_trace_(active_trace),
      id_(id),
      trace_id_(trace_id),
      is_toplevel_(id == trace_id) {}

SpanContext SpanContext::NginxOpenTracingCompatibilityHackSpanContext(
    std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler, uint64_t id,
    uint64_t trace_id) {
  auto active_trace = std::make_shared<ActiveTrace>(logger, nullptr, trace_id);
  SpanContext c = SpanContext{logger, sampler, active_trace, id, trace_id};
  c.nginx_opentracing_compatibility_hack_ = true;
  return c;
}

SpanContext::SpanContext(const SpanContext &other)
    : nginx_opentracing_compatibility_hack_(other.nginx_opentracing_compatibility_hack_),
      logger_(other.logger_),
      sampler_(other.sampler_),
      active_trace_(other.active_trace_),
      id_(other.id_),
      trace_id_(other.trace_id_),
      origin_(other.origin_),
      is_toplevel_(other.is_toplevel_),
      is_extracted_(other.is_extracted_),
      env_(other.env_),
      service_(other.service_),
      name_(other.name_),
      baggage_(other.baggage_) {}

SpanContext &SpanContext::operator=(const SpanContext &other) {
  std::lock_guard<std::mutex> lock{mutex_};
  nginx_opentracing_compatibility_hack_ = other.nginx_opentracing_compatibility_hack_;
  logger_ = other.logger_;
  sampler_ = other.sampler_;
  active_trace_ = other.active_trace_;
  id_ = other.id_;
  trace_id_ = other.trace_id_;
  origin_ = other.origin_;
  is_toplevel_ = other.is_toplevel_;
  is_extracted_ = other.is_extracted_;
  env_ = other.env_;
  service_ = other.service_;
  name_ = other.name_;
  baggage_ = other.baggage_;
  return *this;
}

SpanContext::SpanContext(SpanContext &&other)
    : nginx_opentracing_compatibility_hack_(other.nginx_opentracing_compatibility_hack_),
      logger_(std::move(other.logger_)),
      sampler_(other.sampler_),
      active_trace_(other.active_trace_),
      id_(other.id_),
      trace_id_(other.trace_id_),
      origin_(other.origin_),
      is_toplevel_(other.is_toplevel_),
      is_extracted_(other.is_extracted_),
      env_(other.env_),
      service_(other.service_),
      name_(other.name_),
      baggage_(std::move(other.baggage_)) {}

SpanContext &SpanContext::operator=(SpanContext &&other) {
  std::lock_guard<std::mutex> lock{mutex_};
  nginx_opentracing_compatibility_hack_ = other.nginx_opentracing_compatibility_hack_;
  logger_ = other.logger_;
  sampler_ = other.sampler_;
  active_trace_ = other.active_trace_;
  id_ = other.id_;
  trace_id_ = other.trace_id_;
  origin_ = other.origin_;
  is_toplevel_ = other.is_toplevel_;
  is_extracted_ = other.is_extracted_;
  env_ = other.env_;
  service_ = other.service_;
  name_ = other.name_;
  baggage_ = std::move(other.baggage_);
  return *this;
}

bool SpanContext::operator==(const SpanContext &other) const {
  if (nginx_opentracing_compatibility_hack_ == other.nginx_opentracing_compatibility_hack_ &&
      logger_ == other.logger_ && sampler_ == other.sampler_ &&
      active_trace_ == other.active_trace_ && id_ == other.id_ && trace_id_ == other.trace_id_ &&
      origin_ == other.origin_ && is_toplevel_ == other.is_toplevel_ &&
      is_extracted_ == other.is_extracted_ && env_ == other.env_ && service_ == other.service_ &&
      name_ == other.name_ && baggage_ == other.baggage_) {
    return true;
  }
  return false;
}

bool SpanContext::operator!=(const SpanContext &other) const { return !(*this == other); }

void SpanContext::ForeachBaggageItem(
    std::function<bool(const std::string &, const std::string &)> f) const {
  std::lock_guard<std::mutex> lock{mutex_};
  for (const auto &baggage_item : baggage_) {
    if (!f(baggage_item.first, baggage_item.second)) {
      return;
    }
  }
}

std::unique_ptr<ot::SpanContext> SpanContext::Clone() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return std::unique_ptr<opentracing::SpanContext>(new SpanContext(*this));
}

std::string SpanContext::ToTraceID() const noexcept { return std::to_string(trace_id_); }

std::string SpanContext::ToSpanID() const noexcept { return std::to_string(id_); }

uint64_t SpanContext::id() const {
  // Not locked, since id_ never modified.
  return id_;
}

uint64_t SpanContext::traceId() const {
  // Not locked, since trace_id_ never modified.
  return trace_id_;
}

const std::string SpanContext::origin() const {
  // Not locked, since origin_ never modified.
  return origin_;
}

SpanContext SpanContext::childContext(uint64_t id) const {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  SpanContext child_context(*this);
  child_context.id_ = id;
  if (is_extracted_) {
    child_context.is_toplevel_ = true;
    child_context.is_extracted_ = false;
  } else {
    child_context.is_toplevel_ = false;
  }
  return child_context;
}

std::shared_ptr<ActiveTrace> SpanContext::activeTrace() const { return active_trace_; }

void SpanContext::setEnv(std::string env) { env_ = env; }

void SpanContext::setService(std::string service) { service_ = service; }

void SpanContext::setName(std::string name) { name_ = name; }

void SpanContext::sample() {
  SamplingStatus sampling_status = active_trace_->samplingStatus();
  if (sampling_status.is_set) {
    return;
  }
  auto result = sampler_->sample(env_, service_, name_, trace_id_);
  active_trace_->setSampleResult(result);
}

bool SpanContext::topLevel() const { return is_toplevel_; }

bool SpanContext::extracted() const { return is_extracted_; }

void SpanContext::setBaggageItem(ot::string_view key, ot::string_view value) noexcept try {
  std::lock_guard<std::mutex> lock{mutex_};
  baggage_.emplace(key, value);
} catch (const std::bad_alloc &) {
}

std::string SpanContext::baggageItem(ot::string_view key) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto lookup = baggage_.find(key);
  if (lookup != baggage_.end()) {
    return lookup->second;
  }
  return {};
}

ot::expected<void> SpanContext::serialize(std::ostream &writer) const try {
  // check ostream state
  if (!writer.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  json j;
  // JSON numbers only support 64bit IEEE 754, so we encode these as strings.
  j[json_trace_id_key] = std::to_string(trace_id_);
  j[json_parent_id_key] = std::to_string(id_);
  SamplingStatus sampling_status = active_trace_->samplingStatus();
  if (sampling_status.is_set) {
    j[json_sampling_priority_key] = sampling_status.sample_result.sampling_priority;
    if (!sampling_status.is_propagated) {
      active_trace_->setPropagated();
    }
  }
  if (!origin_.empty()) {
    j[json_origin_key] = origin_;
  }
  j[json_baggage_key] = baggage_;

  writer << j.dump();
  // check ostream state
  if (!writer.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  return {};
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer,
                                          std::set<PropagationStyle> styles) const try {
  ot::expected<void> result;
  for (PropagationStyle style : styles) {
    result = serialize(writer, propagation_headers[style]);
    if (!result) {
      return result;
    }
  }
  return result;
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer,
                                          const HeadersImpl &headers_impl) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto result = writer.Set(headers_impl.trace_id_header, headers_impl.encode_id(trace_id_));
  if (!result) {
    return result;
  }
  result = writer.Set(headers_impl.span_id_header, headers_impl.encode_id(id_));
  if (!result) {
    return result;
  }

  SamplingStatus sampling_status = active_trace_->samplingStatus();
  if (sampling_status.is_set) {
    result = writer.Set(
        headers_impl.sampling_priority_header,
        headers_impl.encode_sampling_priority(sampling_status.sample_result.sampling_priority));
    if (!result) {
      return result;
    }
    if (!sampling_status.is_propagated) {
      active_trace_->setPropagated();
    }
  } else if (nginx_opentracing_compatibility_hack_) {
    // See the comment in the header file on nginx_opentracing_compatibility_hack_.
    result = writer.Set(headers_impl.sampling_priority_header, "1");
    if (!result) {
      return result;
    }
  }
  if (!origin_.empty()) {
    result = writer.Set(headers_impl.origin_header, origin_);
    if (!result) {
      return result;
    }
  }

  for (auto baggage_item : baggage_) {
    std::string key = std::string(baggage_prefix) + baggage_item.first;
    result = writer.Set(key, baggage_item.second);
    if (!result) {
      return result;
    }
  }
  return result;
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
    std::shared_ptr<Writer> writer, std::istream &reader) try {
  // check istream state
  if (!reader.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  // Check for the case when no span is encoded.
  if (reader.eof()) {
    return {};
  }

  uint64_t trace_id, parent_id;
  SamplingStatus sampling_status;
  std::string origin;
  std::unordered_map<std::string, std::string> baggage;
  json j;

  reader >> j;

  if (const auto result = enforce_tag_presence_policy(j.contains(json_trace_id_key),
                                                      j.contains(json_parent_id_key),
                                                      j.contains(json_origin_key))) {
    return std::move(*result);
  }

  std::string trace_id_str = j[json_trace_id_key];
  std::string parent_id_str = j[json_parent_id_key];
  trace_id = std::stoull(trace_id_str);
  parent_id = std::stoull(parent_id_str);

  if (j.find(json_sampling_priority_key) != j.end()) {
    auto sampling_priority = j[json_sampling_priority_key];
    if (!validSamplingPriority(sampling_priority)) {
      // sampling priority value not valid, return unexpected error
      return ot::make_unexpected(ot::span_context_corrupted_error);
    }
    sampling_status.is_set = true;
    sampling_status.is_propagated = true;
    sampling_status.sample_result.sampling_priority = sampling_priority;
  }
  if (j.find(json_origin_key) != j.end()) {
    j.at(json_origin_key).get_to(origin);
  }
  if (j.find(json_baggage_key) != j.end()) {
    j.at(json_baggage_key).get_to(baggage);
  }

  auto active_trace = std::make_shared<ActiveTrace>(logger, writer, trace_id, sampling_status);
  auto context = std::make_unique<SpanContext>(logger, sampler, active_trace, parent_id, trace_id,
                                               origin, std::move(baggage));
  context->is_extracted_ = true;
  return std::unique_ptr<ot::SpanContext>(std::move(context));
} catch (const json::parse_error &) {
  return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
} catch (const std::invalid_argument &ia) {
  return ot::make_unexpected(ot::span_context_corrupted_error);
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
    std::shared_ptr<Writer> writer, const ot::TextMapReader &reader,
    std::set<PropagationStyle> styles) try {
  // TODO(cgilmour): reconsider the handling here, so datadog headers are always prioritized, and
  // aren't expected to have identical IDs/context values as other propagation styles.
  std::unique_ptr<ot::SpanContext> context = nullptr;
  for (PropagationStyle style : styles) {
    auto result =
        SpanContext::deserialize(logger, sampler, writer, reader, propagation_headers[style]);
    if (!result) {
      return ot::make_unexpected(result.error());
    }
    if (result.value() != nullptr) {
      if (context != nullptr && *dynamic_cast<SpanContext *>(result.value().get()) !=
                                    *dynamic_cast<SpanContext *>(context.get())) {
        std::cerr << "Attempt to deserialize SpanContext with conflicting Datadog and B3 headers"
                  << std::endl;
        return ot::make_unexpected(ot::span_context_corrupted_error);
      }
      context = std::move(result.value());
    }
  }
  return context;
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    std::shared_ptr<const Logger> logger, std::shared_ptr<RulesSampler> sampler,
    std::shared_ptr<Writer> writer, const ot::TextMapReader &reader,
    const HeadersImpl &headers_impl) {
  uint64_t trace_id, parent_id;
  SamplingStatus sampling_status;
  std::string origin;
  bool trace_id_set = false;
  bool parent_id_set = false;
  bool origin_set = false;
  std::unordered_map<std::string, std::string> baggage;
  auto result =
      reader.ForeachKey([&](ot::string_view key, ot::string_view value) -> ot::expected<void> {
        try {
          if (equals_ignore_case(key, headers_impl.trace_id_header)) {
            trace_id = std::stoull(value, nullptr, headers_impl.base);
            trace_id_set = true;
          } else if (equals_ignore_case(key, headers_impl.span_id_header)) {
            parent_id = std::stoull(value, nullptr, headers_impl.base);
            parent_id_set = true;
          } else if (equals_ignore_case(key, headers_impl.sampling_priority_header)) {
            auto sampling_priority = std::stoi(value);
            if (!validSamplingPriority(sampling_priority)) {
              // The sampling_priority key was present, but the value makes no sense.
              std::cerr << "Invalid sampling_priority value in serialized SpanContext: "
                        << sampling_priority << std::endl;
              return ot::make_unexpected(ot::span_context_corrupted_error);
            }
            sampling_status.is_set = true;
            sampling_status.is_propagated = true;
            sampling_status.sample_result.sampling_priority =
                static_cast<SamplingPriority>(sampling_priority);
          } else if (headers_impl.origin_header != nullptr &&
                     equals_ignore_case(key, headers_impl.origin_header)) {
            origin = value;
            origin_set = true;
          } else if (has_prefix(key, baggage_prefix)) {
            baggage.emplace(std::string{std::begin(key) + baggage_prefix.size(), std::end(key)},
                            value);
          }
        } catch (const std::invalid_argument &ia) {
          return ot::make_unexpected(ot::span_context_corrupted_error);
        } catch (const std::out_of_range &oor) {
          return ot::make_unexpected(ot::span_context_corrupted_error);
        }
        return {};
      });
  if (!result) {  // "if unexpected", hence "return {}" from above is fine.
    return ot::make_unexpected(result.error());
  }
  if (const auto result = enforce_tag_presence_policy(trace_id_set, parent_id_set, origin_set)) {
    return std::move(*result);
  }
  auto active_trace = std::make_shared<ActiveTrace>(logger, writer, trace_id, sampling_status);
  auto context = std::make_unique<SpanContext>(logger, sampler, active_trace, parent_id, trace_id,
                                               origin, std::move(baggage));
  context->is_extracted_ = true;
  return std::unique_ptr<ot::SpanContext>(std::move(context));
}

}  // namespace opentracing
}  // namespace datadog
