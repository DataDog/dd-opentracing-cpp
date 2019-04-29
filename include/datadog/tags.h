#ifndef DD_INCLUDE_TAGS_H
#define DD_INCLUDE_TAGS_H

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

}  // namespace tags
}  // namespace datadog

#endif  // DD_INCLUDE_TAGS_H
