#include "propagation.h"

#include <algorithm>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

namespace {

// Header names for trace data.
const std::string trace_id_header = "x-datadog-trace-id";
const std::string parent_id_header = "x-datadog-parent-id";
// Header name prefix for OpenTracing baggage. Should be "ot-baggage-" to support OpenTracing
// interop.
const ot::string_view baggage_prefix = "ot-baggage-";

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

SpanContext::SpanContext(uint64_t id, uint64_t trace_id,
                         std::unordered_map<std::string, std::string> &&baggage)
    : id_(id), trace_id_(trace_id), baggage_(std::move(baggage)) {}

SpanContext::SpanContext(SpanContext &&other)
    : id_(other.id_), trace_id_(other.trace_id_), baggage_(std::move(other.baggage_)) {}

SpanContext &SpanContext::operator=(SpanContext &&other) {
  std::lock_guard<std::mutex> lock{mutex_};
  id_ = other.id_;
  trace_id_ = other.trace_id_;
  baggage_ = std::move(other.baggage_);
  return *this;
}

void SpanContext::ForeachBaggageItem(
    std::function<bool(const std::string &, const std::string &)> f) const {
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
  return std::move(SpanContext{id, trace_id_, std::move(baggage)});
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
    const ot::TextMapReader &reader) try {
  uint64_t trace_id, parent_id;
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
  if (!result) {  // "if unexpected", hence "{}" from above is fine.
    return ot::make_unexpected(result.error());
  }
  if (!trace_id_set || !parent_id_set) {
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }
  return std::move(
      std::unique_ptr<ot::SpanContext>{new SpanContext{parent_id, trace_id, std::move(baggage)}});
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
