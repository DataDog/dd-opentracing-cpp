#ifndef DD_OPENTRACING_BOOL_H
#define DD_OPENTRACING_BOOL_H

#include <string>

namespace datadog {
namespace opentracing {

bool stob(const std::string& str, bool fallback);
bool isbool(const std::string& str);

enum class Tribool { False, True, Neither };

Tribool tribool(const std::string&);
Tribool tribool(bool);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_BOOL_H
