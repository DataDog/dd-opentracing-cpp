#ifndef DD_OPENTRACING_UPSTREAM_SERVICE_H
#define DD_OPENTRACING_UPSTREAM_SERVICE_H

// TODO: What is an upstream service?

#include "sampling_mechanism.h"
#include "sampling_priority.h"

#include <opentracing/string_view.h>

#include <string>
#include <vector>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// TODO: Document the class.
struct UpstreamService {
    std::string service_name;
    OptionalSamplingPriority sampling_priority;
    SamplingMechanism sampling_mechanism;
    double sampling_rate;
    // The serialization format for `UpstreamService` allows for the future
    // addition of fields.  When parsing, additional fields that don't
    // correspond to any above are included verbatim in `unknown_fields`.
    std::vector<std::string> unknown_fields;
};

// Return upstream service objects parsed from the specified `text`.  TODO errors.
std::vector<UpstreamService> deserializeUpstreamServices(ot::string_view text);

// Return the result of encoding the specified `upstream_services`.  TODO errors.
std::string serializeUpstreamServices(const std::vector<UpstreamService>& upstream_services);

}  // namespace opentracing
}  // namespace datadog

#endif
