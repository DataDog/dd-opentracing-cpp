#include "spancontext.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

SpanContext::SpanContext(uint64_t id, uint64_t trace_id,
                         std::unordered_map<std::string, std::string> &&baggage) {}

void SpanContext::ForeachBaggageItem(
    std::function<bool(const std::string &, const std::string &)> f) const {}

}  // namespace opentracing
}  // namespace datadog
