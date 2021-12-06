#ifndef DD_OPENTRACING_TAG_PROPAGATION_H
#define DD_OPENTRACING_TAG_PROPAGATION_H

// Some span tags are associated with the entire local trace, rather than just
// a single span within the trace.  These tags are added to the local root span
// before the trace is flushed.
//
// Among these root span tags, some are also propagated as trace context.
// Propagated tags are packaged into the "x-datadog-tags" header in a
// particular format (see the cpp file for a description of the format).
//
// This component provides serialization and deserialization routines for the
// "x-datadog-tags" header format.

#include <opentracing/string_view.h>

#include <string>
#include <unordered_map>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Return a name->value mapping of tags parsed from the specified
// `header_value`.  Throw a `std::runtime_error` if an error occurs.
std::unordered_map<std::string, std::string> deserializeTags(ot::string_view header_value);

// Serialize the tag having the specified `tag_key` and the specified
// `tag_value` and append the result to the specified `serialized_tags`.
void appendTag(std::string& serialized_tags, ot::string_view tag_key, ot::string_view tag_value);

}  // namespace opentracing
}  // namespace datadog

#endif
