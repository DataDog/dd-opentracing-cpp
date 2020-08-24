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

}  // namespace

std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles,
                                                       bool prioritySamplingEnabled) {
  std::vector<ot::string_view> headers;
  for (auto &style : styles) {
    headers.push_back(propagation_headers[style].trace_id_header);
    headers.push_back(propagation_headers[style].span_id_header);
    if (prioritySamplingEnabled) {  // FIXME[willgittoes-dd], ensure this elsewhere
      headers.push_back(propagation_headers[style].sampling_priority_header);
      headers.push_back(propagation_headers[style].origin_header);
    }
  }
  return headers;
}

OptionalSamplingPriority asSamplingPriority(int i) {
  if (i < static_cast<int>(SamplingPriority::MinimumValue) ||
      i > static_cast<int>(SamplingPriority::MaximumValue)) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(static_cast<SamplingPriority>(i));
}

SpanContext::SpanContext(std::shared_ptr<const Logger> logger, uint64_t id, uint64_t trace_id,
                         std::string origin,
                         std::unordered_map<std::string, std::string> &&baggage)
    : logger_(std::move(logger)),
      id_(id),
      trace_id_(trace_id),
      origin_(origin),
      baggage_(std::move(baggage)) {}

SpanContext SpanContext::NginxOpenTracingCompatibilityHackSpanContext(
    std::shared_ptr<const Logger> logger, uint64_t id, uint64_t trace_id,
    std::unordered_map<std::string, std::string> &&baggage) {
  SpanContext c = SpanContext{logger, id, trace_id, "", std::move(baggage)};
  c.nginx_opentracing_compatibility_hack_ = true;
  return c;
}

SpanContext::SpanContext(SpanContext &&other)
    : nginx_opentracing_compatibility_hack_(other.nginx_opentracing_compatibility_hack_),
      logger_(other.logger_),
      id_(other.id_),
      trace_id_(other.trace_id_),
      propagated_sampling_priority_(std::move(other.propagated_sampling_priority_)),
      origin_(other.origin_),
      baggage_(std::move(other.baggage_)) {}

SpanContext &SpanContext::operator=(SpanContext &&other) {
  std::lock_guard<std::mutex> lock{mutex_};
  logger_ = other.logger_;
  id_ = other.id_;
  trace_id_ = other.trace_id_;
  origin_ = other.origin_;
  propagated_sampling_priority_ = std::move(other.propagated_sampling_priority_);
  baggage_ = std::move(other.baggage_);
  nginx_opentracing_compatibility_hack_ = other.nginx_opentracing_compatibility_hack_;
  return *this;
}

bool SpanContext::operator==(const SpanContext &other) const {
  if (logger_ != other.logger_ || id_ != other.id_ || trace_id_ != other.trace_id_ ||
      baggage_ != other.baggage_ ||
      nginx_opentracing_compatibility_hack_ != other.nginx_opentracing_compatibility_hack_) {
    return false;
  }
  if (propagated_sampling_priority_ == nullptr) {
    return other.propagated_sampling_priority_ == nullptr;
  }
  return other.propagated_sampling_priority_ != nullptr &&
         *propagated_sampling_priority_ == *other.propagated_sampling_priority_ &&
         origin_ == other.origin_;
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

uint64_t SpanContext::id() const {
  // Not locked, since id_ never modified.
  return id_;
}

uint64_t SpanContext::traceId() const {
  // Not locked, since trace_id_ never modified.
  return trace_id_;
}

OptionalSamplingPriority SpanContext::getPropagatedSamplingPriority() const {
  // Not locked. Both these members are only ever written in the constructor/builder.
  OptionalSamplingPriority p = nullptr;
  if (propagated_sampling_priority_ != nullptr) {
    p.reset(new SamplingPriority(*propagated_sampling_priority_));
  }
  return p;
}

const std::string SpanContext::origin() const {
  // Not locked, since origin_ never modified.
  return origin_;
}

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

SpanContext SpanContext::withId(uint64_t id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto baggage = baggage_;  // (Shallow) copy baggage.
  SpanContext context{logger_, id, trace_id_, origin_, std::move(baggage)};
  if (propagated_sampling_priority_ != nullptr) {
    context.propagated_sampling_priority_.reset(
        new SamplingPriority(*propagated_sampling_priority_));
  }
  return context;
}

ot::expected<void> SpanContext::serialize(std::ostream &writer,
                                          const std::shared_ptr<SpanBuffer> pending_traces,
                                          bool prioritySamplingEnabled) const try {
  // check ostream state
  if (!writer.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  json j;
  // JSON numbers only support 64bit IEEE 754, so we encode these as strings.
  j[json_trace_id_key] = std::to_string(trace_id_);
  j[json_parent_id_key] = std::to_string(id_);
  OptionalSamplingPriority sampling_priority = pending_traces->getSamplingPriority(trace_id_);
  if (sampling_priority != nullptr && prioritySamplingEnabled) {
    j[json_sampling_priority_key] = static_cast<int>(*sampling_priority);
    if (!origin_.empty()) {
      j[json_origin_key] = origin_;
    }
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
                                          const std::shared_ptr<SpanBuffer> pending_traces,
                                          std::set<PropagationStyle> styles,
                                          bool prioritySamplingEnabled) const try {
  ot::expected<void> result;
  for (PropagationStyle style : styles) {
    result =
        serialize(writer, pending_traces, propagation_headers[style], prioritySamplingEnabled);
    if (!result) {
      return result;
    }
  }
  return result;
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer,
                                          const std::shared_ptr<SpanBuffer> pending_traces,
                                          const HeadersImpl &headers_impl,
                                          bool prioritySamplingEnabled) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto result = writer.Set(headers_impl.trace_id_header, headers_impl.encode_id(trace_id_));
  if (!result) {
    return result;
  }
  result = writer.Set(headers_impl.span_id_header, headers_impl.encode_id(id_));
  if (!result) {
    return result;
  }

  if (prioritySamplingEnabled) {
    OptionalSamplingPriority sampling_priority = pending_traces->getSamplingPriority(trace_id_);
    if (sampling_priority != nullptr) {
      result = writer.Set(headers_impl.sampling_priority_header,
                          headers_impl.encode_sampling_priority(*sampling_priority));
      if (!result) {
        return result;
      }
      if (!origin_.empty()) {
        result = writer.Set(headers_impl.origin_header, origin_);
        if (!result) {
          return result;
        }
      }
    } else if (nginx_opentracing_compatibility_hack_) {
      // See the comment in the header file on nginx_opentracing_compatibility_hack_.
      result = writer.Set(headers_impl.sampling_priority_header, "1");
      if (!result) {
        return result;
      }
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
    std::shared_ptr<const Logger> logger, std::istream &reader) try {
  // check istream state
  if (!reader.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  // Check for the case when no span is encoded.
  if (reader.eof()) {
    return {};
  }

  uint64_t trace_id, parent_id;
  OptionalSamplingPriority sampling_priority = nullptr;
  std::string origin;
  std::unordered_map<std::string, std::string> baggage;
  json j;

  reader >> j;
  bool trace_id_set = j.find(json_trace_id_key) != j.end();
  bool parent_id_set = j.find(json_parent_id_key) != j.end();
  bool sampling_priority_set = j.find(json_sampling_priority_key) != j.end();
  bool origin_set = j.find(json_origin_key) != j.end();

  if (!trace_id_set && !parent_id_set) {
    // both ids empty, return empty context
    return {};
  }
  if (!trace_id_set || !parent_id_set) {
    // missing one id, return unexpected error
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }
  if (origin_set && !sampling_priority_set) {
    // origin should only be set if sampling priority is also set.
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }

  std::string trace_id_str = j[json_trace_id_key];
  std::string parent_id_str = j[json_parent_id_key];
  trace_id = std::stoull(trace_id_str);
  parent_id = std::stoull(parent_id_str);

  if (j.find(json_sampling_priority_key) != j.end()) {
    sampling_priority = asSamplingPriority(j[json_sampling_priority_key]);
    if (sampling_priority == nullptr) {
      // sampling priority value not valid, return unexpected error
      return ot::make_unexpected(ot::span_context_corrupted_error);
    }
  }
  if (j.find(json_origin_key) != j.end()) {
    j.at(json_origin_key).get_to(origin);
  }
  if (j.find(json_baggage_key) != j.end()) {
    j.at(json_baggage_key).get_to(baggage);
  }

  auto context =
      std::make_unique<SpanContext>(logger, parent_id, trace_id, origin, std::move(baggage));
  context->propagated_sampling_priority_ = std::move(sampling_priority);
  return std::unique_ptr<ot::SpanContext>(std::move(context));
} catch (const json::parse_error &) {
  return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
} catch (const std::invalid_argument &ia) {
  return ot::make_unexpected(ot::span_context_corrupted_error);
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    std::shared_ptr<const Logger> logger, const ot::TextMapReader &reader,
    std::set<PropagationStyle> styles) try {
  std::unique_ptr<ot::SpanContext> context = nullptr;
  for (PropagationStyle style : styles) {
    auto result = SpanContext::deserialize(logger, reader, propagation_headers[style]);
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
    std::shared_ptr<const Logger> logger, const ot::TextMapReader &reader,
    const HeadersImpl &headers_impl) {
  uint64_t trace_id, parent_id;
  OptionalSamplingPriority sampling_priority = nullptr;
  std::string origin;
  bool trace_id_set = false;
  bool parent_id_set = false;
  bool sampling_priority_set = false;
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
            sampling_priority = asSamplingPriority(std::stoi(value));
            if (sampling_priority == nullptr) {
              // The sampling_priority key was present, but the value makes no sense.
              std::cerr << "Invalid sampling_priority value in serialized SpanContext"
                        << std::endl;
              return ot::make_unexpected(ot::span_context_corrupted_error);
            }
            sampling_priority_set = true;
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
  if (!trace_id_set && !parent_id_set) {
    return {};  // Empty context/no context provided.
  }
  if (!trace_id_set || !parent_id_set) {
    // Partial context, this shouldn't happen.
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }
  if (origin_set && !sampling_priority_set) {
    // Origin header should only be set if sampling priority is also set.
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }
  auto context =
      std::make_unique<SpanContext>(logger, parent_id, trace_id, origin, std::move(baggage));
  context->propagated_sampling_priority_ = std::move(sampling_priority);
  return std::unique_ptr<ot::SpanContext>(std::move(context));
}

}  // namespace opentracing
}  // namespace datadog
