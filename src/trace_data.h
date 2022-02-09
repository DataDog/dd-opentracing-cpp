#ifndef DD_OPENTRACING_TRACE_DATA_H
#define DD_OPENTRACING_TRACE_DATA_H

// This component defines a container for span data associated with the same
// trace.

#include <memory>
#include <vector>

namespace datadog {
namespace opentracing {

struct SpanData;  // see span.h

using TraceData = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

}  // namespace opentracing
}  // namespace datadog

#endif
