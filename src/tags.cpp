#include <datadog/tags.h>

#include <string>

namespace datadog {
namespace tags {

const std::string environment = "env";
const std::string service_name = "service.name";
const std::string span_type = "span.type";
const std::string operation_name = "operation";
const std::string resource_name = "resource.name";
const std::string analytics_event = "analytics.event";
const std::string manual_keep = "manual.keep";
const std::string manual_drop = "manual.drop";
const std::string version = "version";

}  // namespace tags
}  // namespace datadog
