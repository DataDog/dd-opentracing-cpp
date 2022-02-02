#include "parse_util.h"

#include <algorithm>
#include <stdexcept>

namespace datadog {
namespace opentracing {

uint64_t parse_uint64(const std::string &text, int base) {
  std::size_t end_index;
  const uint64_t result = std::stoull(text, &end_index, base);

  // If any of the remaining characters are not whitespace, then `text`
  // contains something other than a base-`base` integer.
  if (std::any_of(text.begin() + end_index, text.end(),
                  [](unsigned char ch) { return !std::isspace(ch); })) {
    throw std::invalid_argument("integer text field has a trailing non-whitespace character");
  }

  return result;
}

}  // namespace opentracing
}  // namespace datadog
