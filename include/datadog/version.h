#ifndef DD_INCLUDE_VERSION_H
#define DD_INCLUDE_VERSION_H

#include <string>

namespace datadog {
namespace version {

const std::string tracer_version = "v1.1.5";
const std::string cpp_version = std::to_string(__cplusplus);

}  // namespace version
}  // namespace datadog

#endif  // DD_INCLUDE_VERSION_H
