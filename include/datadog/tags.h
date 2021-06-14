#ifndef DD_INCLUDE_TAGS_H
#define DD_INCLUDE_TAGS_H

#include <string>

namespace datadog {
namespace tags {

extern const std::string environment;
extern const std::string service_name;
extern const std::string span_type;
extern const std::string operation_name;
extern const std::string resource_name;
extern const std::string analytics_event;
extern const std::string manual_keep;
extern const std::string manual_drop;
extern const std::string version;

}  // namespace tags
}  // namespace datadog

#endif  // DD_INCLUDE_TAGS_H
