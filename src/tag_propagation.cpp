#include "tag_propagation.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <sstream>
#include <stdexcept>

namespace datadog {
namespace opentracing {

// The following [eBNF][1] grammar describes the tag propagation encoding.
// The grammar was copied from [an internal design document][2].
//
//     tagset = ( tag, { ",", tag } ) | "";
//     tag = ( identifier - space or equal ), "=", identifier;
//     identifier = allowed characters, { allowed characters };
//     allowed characters = ( ? ASCII characters 32-126 ? - "," );
//     space or equal = " " | "=";
//
// That is, comma-separated "<key>=<value>" pairs.
//
// See `tag_propagation_test.cpp` for examples.
//
// [1]: https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form
// [2]:
// https://docs.google.com/document/d/1zeO6LGnvxk5XweObHAwJbK3SfK23z7jQzp7ozWJTa2A/edit#heading=h.yp07yuixga36

namespace {

// Return a `string_view` over the specified range of characters `[begin, end)`.
ot::string_view range(const char* begin, const char* end) {
  assert(begin <= end);
  return ot::string_view{begin, std::size_t(end - begin)};
}

// Insert into the specified `destination` a tag decoded from the specified
// `entry`.  Throw a `std::invalid_argument` if an error occurs.
void deserializeTag(std::unordered_map<std::string, std::string>& destination,
                    ot::string_view entry) {
  const auto separator = std::find(entry.begin(), entry.end(), '=');
  if (separator == entry.end()) {
    std::ostringstream error;
    error << "invalid key=value pair for encoded tag: missing \"=\" in: " << entry;
    throw std::invalid_argument(error.str());
  }

  const ot::string_view key = range(entry.begin(), separator);
  const ot::string_view value = range(separator + 1, entry.end());
  // Among duplicate keys, most recent value wins.
  destination[key] = value;
}

}  // namespace

std::unordered_map<std::string, std::string> deserializeTags(ot::string_view header_value) {
  std::unordered_map<std::string, std::string> tags;

  auto iter = header_value.begin();
  const auto end = header_value.end();
  if (iter == end) {
    // An empty string means no tags.
    return tags;
  }

  decltype(iter) next;
  do {
    next = std::find(iter, end, ',');
    deserializeTag(tags, range(iter, next));
    iter = next + 1;
  } while (next != end);

  return tags;
}

void appendTag(std::string& serialized_tags, ot::string_view tag_key, ot::string_view tag_value) {
  if (!serialized_tags.empty()) {
    serialized_tags += ',';
  }
  serialized_tags.append(tag_key.begin(), tag_key.end());
  serialized_tags += '=';
  serialized_tags.append(tag_value.begin(), tag_value.end());
}

}  // namespace opentracing
}  // namespace datadog
