#ifndef DD_OPENTRACING_BASE64_RFC4648_UNPADDED_H
#define DD_OPENTRACING_BASE64_RFC4648_UNPADDED_H

#include <opentracing/string_view.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Encode the specified `source` data as base64, where the encoding is
// compatible with RFC 4648 _except_ that any trailing padding characters are
// omitted.  Append to the specified `destination` the encoded result,
// expanding the storage of `destination` as needed.
void appendBase64Unpadded(std::string& destination, ot::string_view source);

}  // namespace opentracing
}  // namespace datadog

#endif
