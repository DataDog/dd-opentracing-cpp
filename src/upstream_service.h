#ifndef DD_OPENTRACING_UPSTREAM_SERVICE_H
#define DD_OPENTRACING_UPSTREAM_SERVICE_H

// This component provides a type, `UpstreamService`, that contains the
// sampling decision made by a service that preceded us in the current trace.
// A subset of the prefix of all upstream services for the current trace is
// propagated from service to service along the trace.  An `UpstreamService`
// object is added to an encoded list using the `appendUpstreamService`
// function.
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
  // `sampling_mechanism` could be one of the known `SamplingMechanism` values,
  // or some other (future) value.
  int sampling_mechanism;
  double sampling_rate;  // `std::nan("")` means "no sampling rate"
};

// Extend the specified `destination` to contain the encoded value of the
// specified `upstream_service`.  If `destination` is not empty, then a
// delimiter character will be added before the encoded `upstream_service`.
void appendUpstreamService(std::string& destination, const UpstreamService& upstream_service);

// Round the specified sampling rate `value` up to the fourth decimal place,
// format it as a decimal with at most four significant digits, and append the
// result to the specified `destination`.
// This function is exposed for use in unit tests.
void appendSamplingRate(std::string& destination, double value);

}  // namespace opentracing
}  // namespace datadog

#endif
