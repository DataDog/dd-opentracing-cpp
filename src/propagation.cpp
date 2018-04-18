#include "propagation.h"

#include <algorithm>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

namespace {

// Header names for trace data.
const std::string trace_id_header = "x-datadog-trace-id";
const std::string parent_id_header = "x-datadog-parent-id";

// Does what it says on the tin. Just looks at each char, so don't try and use this on
// unicode strings, only used for comparing HTTP header names.
// Rolled my own because I don't want to import all of libboost for one function!
bool equals_ignore_case(const std::string &a, const std::string &b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](char a, char b) { return tolower(a) == tolower(b); });
}

}  // namespace

SpanContext::SpanContext(uint64_t id, uint64_t trace_id,
                         std::unordered_map<std::string, std::string> &&baggage)
    : id_(id), trace_id_(trace_id) {}

void SpanContext::ForeachBaggageItem(
    std::function<bool(const std::string &, const std::string &)> f) const {}

uint64_t SpanContext::id() const { return id_; }
uint64_t SpanContext::trace_id() const { return trace_id_; }

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer) const {
  auto result = writer.Set(trace_id_header, std::to_string(trace_id_));
  if (!result) {
    return result;
  }
  // Yes, "id" does go to "parent id" since this is the point where subsequent Spans getting this
  // context become children.
  result = writer.Set(parent_id_header, std::to_string(id_));
  return result;
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    const ot::TextMapReader &reader) try {
  uint64_t trace_id, parent_id;
  bool trace_id_set = false;
  bool parent_id_set = false;
  auto result =
      reader.ForeachKey([&](ot::string_view key, ot::string_view value) -> ot::expected<void> {
        try {
          if (equals_ignore_case(key, trace_id_header)) {
            trace_id = std::stoull(value);
            trace_id_set = true;
          } else if (equals_ignore_case(key, parent_id_header)) {
            parent_id = std::stoull(value);
            parent_id_set = true;
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
  return std::move(std::unique_ptr<ot::SpanContext>{new SpanContext{parent_id, trace_id, {}}});
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
