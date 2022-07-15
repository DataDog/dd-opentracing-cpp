#include "glob.h"

#include <cstdint>

namespace datadog {
namespace opentracing {

bool glob_match(ot::string_view pattern, ot::string_view subject) {
  // This is a backtracking implementation of the glob matching algorithm.
  // The glob pattern language supports `*` and `?`, but no escape sequences.
  //
  // Based off of a Go example in <https://research.swtch.com/glob> accessed
  // February 3, 2022.

  using Index = std::size_t;
  Index p = 0;       // [p]attern index
  Index s = 0;       // [s]ubject index
  Index next_p = 0;  // next [p]attern index
  Index next_s = 0;  // next [s]ubject index

  while (p < pattern.size() || s < subject.size()) {
    if (p < pattern.size()) {
      const char pattern_char = pattern[p];
      switch (pattern_char) {
        case '*':
          // Try to match at `s`.  If that doesn't work out, restart at
          // `s + 1` next.
          next_p = p;
          next_s = s + 1;
          ++p;
          continue;
        case '?':
          if (s < subject.size()) {
            ++p;
            ++s;
            continue;
          }
          break;
        default:
          if (s < subject.size() && subject[s] == pattern_char) {
            ++p;
            ++s;
            continue;
          }
      }
    }
    // Mismatch.  Maybe restart.
    if (0 < next_s && next_s <= subject.size()) {
      p = next_p;
      s = next_s;
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace opentracing
}  // namespace datadog
