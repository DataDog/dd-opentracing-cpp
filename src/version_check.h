#ifndef DD_OPENTRACING_VERSION_CHECK_H
#define DD_OPENTRACING_VERSION_CHECK_H

#include <string>

namespace datadog {
namespace opentracing {

bool equal_or_higher_version(const std::string actual, const std::string min);

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_VERSION_CHECK_H
