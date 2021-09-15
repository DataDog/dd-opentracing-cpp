#include <datadog/version.h>

#include <string>

namespace datadog {
namespace version {

const std::string tracer_version = "v1.3.2";
const std::string cpp_version = std::to_string(__cplusplus);

}  // namespace version
}  // namespace datadog
