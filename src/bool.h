#ifndef DD_OPENTRACING_BOOL_H
#define DD_OPENTRACING_BOOL_H

namespace datadog {
namespace opentracing {

bool stob(const std::string& str, bool fallback);
bool isbool(const std::string& str);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_BOOL_H
