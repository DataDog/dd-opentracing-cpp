#ifndef DD_OPENTRACING_PARSE_UTIL_H
#define DD_OPENTRACING_PARSE_UTIL_H

// This component provides parsing routines used by other components.

#include <cstdint>
#include <string>

namespace datadog {
namespace opentracing {

// Interpret the specified `text` as a non-negative integer formatted in the
// specified `base` (e.g. base 10 for decimal, base 16 for hexadecimal),
// possibly surrounded by whitespace, and return the integer.  Throw an
// exception derived from `std::logic_error` if an error occurs.
uint64_t parse_uint64(const std::string &text, int base);

}  // namespace opentracing
}  // namespace datadog

#endif
