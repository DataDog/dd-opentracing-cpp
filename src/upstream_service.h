#ifndef DD_OPENTRACING_UPSTREAM_SERVICE_H
#define DD_OPENTRACING_UPSTREAM_SERVICE_H

// This component provides a type, `UpstreamService`, that contains the
// sampling decision made by a service that preceded us in the current trace.
// A subset of the prefix of all upstream services for the current trace is
// propagated from service to service along the trace.  The upstream services
// are encoded to and decoded from their propagation format by the provided
// functions `serializeUpstreamServices` and `deserializeUpstreamServices`,
// respectively.
//
// In addition to being propagated along the trace, upstream services are also
// sent to the agent as a span tag, "_dd.p.upstream_services", in the local
// root span.

#include <opentracing/string_view.h>

#include <string>
#include <vector>

#include "sampling_mechanism.h"
#include "sampling_priority.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// `upstream_services_tag` is the name of the tag in which `UpstreamService`
// objects are encoded.
extern const ot::string_view upstream_services_tag;

// `UpstreamService` contains the sampling decision made by a service that
// preceded us in the current trace.
struct UpstreamService {
  std::string service_name;
  SamplingPriority sampling_priority;
  SamplingMechanism sampling_mechanism;
  double sampling_rate;  // `std::nan("")` means "no sampling rate"
  // The serialization format for `UpstreamService` allows for the future
  // addition of fields.  When parsing, additional fields that don't
  // correspond to any above are included verbatim in `unknown_fields`.
  std::vector<std::string> unknown_fields;
};

// Return whether the specified `left` and the specified `right` have the same
// value.  Two `UpstreamService` objects have the same value when their
// serialized values are the same.
bool operator==(const UpstreamService& left, const UpstreamService& right);

// Return upstream service objects parsed from the specified `text`, or throw a
// `std::runtime_error` if an error occurs.
std::vector<UpstreamService> deserializeUpstreamServices(ot::string_view text);

// Return the result of encoding the specified `upstream_services`.
std::string serializeUpstreamServices(const std::vector<UpstreamService>& upstream_services);

// Round the specified sampling rate `value` up to the fourth decimal place,
// format it as a decimal with at most four significant digits, and append the
// result to the specified `destination`.
// This function is exposed for use in unit tests.
void appendSamplingRate(std::string& destination, double value);

}  // namespace opentracing
}  // namespace datadog

#endif
