#include "propagation.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {

// Header names for trace data.
const std::string trace_id_header = "x-datadog-trace-id";
const std::string parent_id_header = "x-datadog-parent-id";
const std::string sampling_priority_header = "x-datadog-sampling-priority";
// Header name prefix for OpenTracing baggage. Should be "ot-baggage-" to support OpenTracing
// interop.
const ot::string_view baggage_prefix = "ot-baggage-";

// Key names for binary serialization in JSON
const std::string json_trace_id_key = "trace_id";
const std::string json_parent_id_key = "parent_id";
const std::string json_sampling_priority_key = "sampling_priority";
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

OptionalSamplingPriority asSamplingPriority(int i) {
  if (i < (int)SamplingPriority::MinimumValue || i > (int)SamplingPriority::MaximumValue) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(static_cast<SamplingPriority>(i));
}

SpanContext::SpanContext(uint64_t id, uint64_t trace_id,
                         OptionalSamplingPriority sampling_priority,
                         std::unordered_map<std::string, std::string> &&baggage)
    : id_(id),
      trace_id_(trace_id),
      sampling_priority_(std::move(sampling_priority)),
      baggage_(std::move(baggage)) {}

SpanContext SpanContext::NginxOpenTracingCompatibilityHackSpanContext(
    uint64_t id, uint64_t trace_id, OptionalSamplingPriority sampling_priority,
    std::unordered_map<std::string, std::string> &&baggage) {
  SpanContext c = SpanContext(id, trace_id, std::move(sampling_priority), std::move(baggage));
  c.nginx_opentracing_compatibility_hack_ = true;
  return c;
}

SpanContext::SpanContext(SpanContext &&other)
    : id_(other.id_),
      trace_id_(other.trace_id_),
      sampling_priority_(std::move(other.sampling_priority_)),
      baggage_(std::move(other.baggage_)),
      nginx_opentracing_compatibility_hack_(other.nginx_opentracing_compatibility_hack_) {}

SpanContext &SpanContext::operator=(SpanContext &&other) {
  std::lock_guard<std::mutex> lock{mutex_};
  id_ = other.id_;
  trace_id_ = other.trace_id_;
  sampling_priority_ = std::move(other.sampling_priority_);
  baggage_ = std::move(other.baggage_);
  nginx_opentracing_compatibility_hack_ = other.nginx_opentracing_compatibility_hack_;
  return *this;
}

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

uint64_t SpanContext::trace_id() const {
  // Not locked, since trace_id_ never modified.
  return trace_id_;
}

OptionalSamplingPriority SpanContext::getSamplingPriority() const {
  std::lock_guard<std::mutex> lock{mutex_};
  if (sampling_priority_ == nullptr) {
    return nullptr;
  }
  return std::make_unique<SamplingPriority>(*sampling_priority_);
}

void SpanContext::setSamplingPriority(OptionalSamplingPriority p) {
  std::lock_guard<std::mutex> lock{mutex_};
  if (p == nullptr) {
    sampling_priority_.reset(nullptr);
  } else {
    sampling_priority_.reset(new SamplingPriority(*p));
  }
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
  // Copy sampling_priority_.
  std::unique_ptr<SamplingPriority> p = nullptr;
  if (sampling_priority_ != nullptr) {
    p.reset(new SamplingPriority(*sampling_priority_));
  }
  return SpanContext{id, trace_id_, std::move(p), std::move(baggage)};
}

ot::expected<void> SpanContext::serialize(std::ostream &writer) const {
  // check ostream state
  if (!writer.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  json j;
  // JSON numbers only support 64bit IEEE 754, so we encode these as strings.
  j[json_trace_id_key] = std::to_string(trace_id_);
  j[json_parent_id_key] = std::to_string(id_);
  if (sampling_priority_ != nullptr) {
    j[json_sampling_priority_key] = static_cast<int>(*sampling_priority_);
  }
  j[json_baggage_key] = baggage_;

  writer << j.dump();
  // check ostream state
  if (!writer.good()) {
    return ot::make_unexpected(std::make_error_code(std::errc::io_error));
  }

  return {};
}

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto result = writer.Set(trace_id_header, std::to_string(trace_id_));
  if (!result) {
    return result;
  }
  // Yes, "id" does go to "parent id" since this is the point where subsequent Spans getting this
  // context become children.
  result = writer.Set(parent_id_header, std::to_string(id_));
  if (!result) {
    return result;
  }

  if (sampling_priority_ != nullptr) {
    result = writer.Set(sampling_priority_header,
                        std::to_string(static_cast<int>(*sampling_priority_)));
    if (!result) {
      return result;
    }
  } else if (nginx_opentracing_compatibility_hack_) {
    // See the comment in the header file on nginx_opentracing_compatibility_hack_.
    result = writer.Set(sampling_priority_header, "1");
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

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(std::istream &reader) try {
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
  std::unordered_map<std::string, std::string> baggage;
  json j;

  reader >> j;
  bool trace_id_set = j.find(json_trace_id_key) != j.end();
  bool parent_id_set = j.find(json_parent_id_key) != j.end();

  if (!trace_id_set && !parent_id_set) {
    // both ids empty, return empty context
    return {};
  }
  if (!trace_id_set || !parent_id_set) {
    // missing one id, return unexpected error
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
  if (j.find(json_baggage_key) != j.end()) {
    baggage = j[json_baggage_key].get<std::unordered_map<std::string, std::string>>();
  }

  return std::unique_ptr<ot::SpanContext>(std::make_unique<SpanContext>(
      parent_id, trace_id, std::move(sampling_priority), std::move(baggage)));

} catch (const json::parse_error &) {
  return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
} catch (const std::invalid_argument &ia) {
  return ot::make_unexpected(ot::span_context_corrupted_error);
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    const ot::TextMapReader &reader) try {
  uint64_t trace_id, parent_id;
  OptionalSamplingPriority sampling_priority = nullptr;
  bool trace_id_set = false;
  bool parent_id_set = false;
  std::unordered_map<std::string, std::string> baggage;
  auto result =
      reader.ForeachKey([&](ot::string_view key, ot::string_view value) -> ot::expected<void> {
        try {
          if (equals_ignore_case(key, trace_id_header)) {
            trace_id = std::stoull(value);
            trace_id_set = true;
          } else if (equals_ignore_case(key, parent_id_header)) {
            parent_id = std::stoull(value);
            parent_id_set = true;
          } else if (equals_ignore_case(key, sampling_priority_header)) {
            sampling_priority = asSamplingPriority(std::stoi(value));
            if (sampling_priority == nullptr) {
              // The sampling_priority key was present, but the value makes no sense.
              std::cerr << "Invalid sampling_priority value in serialized SpanContext"
                        << std::endl;
              return ot::make_unexpected(ot::span_context_corrupted_error);
            }
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
  return std::unique_ptr<ot::SpanContext>(std::make_unique<SpanContext>(
      parent_id, trace_id, std::move(sampling_priority), std::move(baggage)));
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
