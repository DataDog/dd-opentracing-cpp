#include "bool.h"

#include <unordered_map>

namespace datadog {
namespace opentracing {

namespace {
std::unordered_map<std::string, bool> conversions{
    {"1", true},  {"t", true},  {"T", true},  {"true", true},   {"TRUE", true},   {"True", true},
    {"0", false}, {"f", false}, {"F", false}, {"false", false}, {"FALSE", false}, {"False", false},
};
}  // namespace

bool stob(const std::string& str, bool fallback) {
  switch (tribool(str)) {
    case Tribool::True:
      return true;
    case Tribool::False:
      return false;
    default:
      return fallback;
  }
}

bool isbool(const std::string& str) { return tribool(str) != Tribool::Neither; }

Tribool tribool(bool value) { return value ? Tribool::True : Tribool::False; }

Tribool tribool(const std::string& str) {
  auto entry = conversions.find(str);
  if (entry == conversions.end()) {
    return Tribool::Neither;
  }
  return tribool(entry->second);
}

}  // namespace opentracing
}  // namespace datadog
