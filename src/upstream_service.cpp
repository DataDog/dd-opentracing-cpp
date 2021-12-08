#include "upstream_service.h"

namespace datadog {
namespace opentracing {
namespace {

UpstreamService deserialize(ot::string_view text) {
    // TODO
    (void)text;
    return {};
}

std::string serialize(const UpstreamService& upstream_service) {
     // TODO
     (void)upstream_service;
     return "";
}

}  // namespace

std::vector<UpstreamService> deserializeUpstreamServices(ot::string_view text) {
    // TODO
    (void)text;
    (void)deserialize(text);
    return {};
}

std::string serializeUpstreamServices(const std::vector<UpstreamService>& upstream_services) {
    // TODO
    (void)upstream_services;
    (void)serialize(upstream_services.front());
    return "";
}

}  // namespace opentracing
}  // namespace datadog
