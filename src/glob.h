#ifndef DD_OPENTRACING_GLOB_H
#define DD_OPENTRACING_GLOB_H

// This component provides a string matching function, `glob_match`, that
// returns whether a specified string matches a specified pattern, where the
// pattern language is the following:
//
// - "*" matches any contiguous substring, including the empty string.
// - "?" matches exactly one instance of any character.
// - Other characters match exactly one instance of themselves.
//
// The patterns are here called "glob patterns," though they are different from
// the patterns used in Unix shells.

#include <opentracing/string_view.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Return whether the specified `subject` matches the specified glob `pattern`,
// i.e.  whether `subject` is a member of the set of strings represented by the
// glob `pattern`.
bool glob_match(ot::string_view pattern, ot::string_view subject);

}  // namespace opentracing
}  // namespace datadog

#endif
