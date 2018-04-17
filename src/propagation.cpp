#include "propagation.h"

#include <algorithm>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

namespace {

const std::string trace_id_header = "x-datadog-trace-id";
const std::string parent_id_header = "x-datadog-parent-id";

// Because I don't want to import all of libboost for one function!
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

ot::expected<void> SpanContext::serialize(const ot::TextMapWriter &writer) const {
  auto result = writer.Set(trace_id_header, std::to_string(trace_id_));
  if (!result) {
    return result;
  }
  // Yes, "id" does go to "parent id". Since this is the point where subsequent Spans getting this
  // context become children.
  result = writer.Set(parent_id_header, std::to_string(id_));
  return result;
}

ot::expected<std::unique_ptr<ot::SpanContext>> SpanContext::deserialize(
    const ot::TextMapReader &reader) try {
  uint64_t trace_id, parent_id;
  int missing_required_keys = 2;  // We want both trace_id and parent_id.
  auto result =
      reader.ForeachKey([&](ot::string_view key, ot::string_view value) -> ot::expected<void> {
        try {
          if (equals_ignore_case(key, trace_id_header)) {
            trace_id = std::stoull(value);
            missing_required_keys--;
          } else if (equals_ignore_case(key, parent_id_header)) {
            parent_id = std::stoull(value);
            missing_required_keys--;
          }
        } catch (const std::invalid_argument &ia) {
          return ot::make_unexpected(ot::span_context_corrupted_error);
        } catch (const std::out_of_range &oor) {
          return ot::make_unexpected(ot::span_context_corrupted_error);
        }
        return {};
      });
  if (!result) {
    return ot::make_unexpected(result.error());
  }
  if (missing_required_keys) {
    return ot::make_unexpected(ot::span_context_corrupted_error);
  }
  return std::move(std::unique_ptr<ot::SpanContext>{new SpanContext{parent_id, trace_id, {}}});
} catch (const std::bad_alloc &) {
  return ot::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
}

}  // namespace opentracing
}  // namespace datadog
