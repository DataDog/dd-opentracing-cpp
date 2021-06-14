#ifndef DD_OPENTRACING_STRING_VIEW_H
#define DD_OPENTRACING_STRING_VIEW_H

#include <cstring>
#include <opentracing/string_view.h>
#include <string>

// This header defines comparison operators for `opentracing::string_view` that
// are missing from the upstream library.  It does not define a DataDog-specific
// `string_view` type.  This library uses the upstream
// `opentracing::string_view` type.

namespace opentracing {

inline bool operator<(string_view lhs, string_view rhs) noexcept {
  const int common_length_cmp =
      std::strncmp(lhs.data(), rhs.data(), std::min(lhs.size(), rhs.size()));
  
  if (common_length_cmp == 0) {
    return lhs.size() < rhs.size();
  }

  return common_length_cmp < 0;
}

inline bool operator<(string_view left, const std::string& right) noexcept {
  return left < string_view(right);
}

inline bool operator<(const std::string& left, string_view right) noexcept {
  return string_view(left) < right;
}

} // namespace opentracing

#endif
