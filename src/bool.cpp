#include <string>
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
  if (str.empty()) {
    return fallback;
  }
  auto result = conversions.find(str);
  if (result == conversions.end()) {
    return fallback;
  }
  return result->second;
}

bool isbool(const std::string& str) { return conversions.find(str) != conversions.end(); }

}  // namespace opentracing
}  // namespace datadog
