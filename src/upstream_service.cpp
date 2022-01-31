#include "upstream_service.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>

#include "base64_rfc4648_unpadded.h"

namespace datadog {
namespace opentracing {

const ot::string_view upstream_services_tag = "_dd.p.upstream_services";

// The following [eBNF][1] grammar describes the upstream services encoding.
// The grammar was copied from [an internal design document][2].
//
//     upstream services  =  ( group, { ";", group } ) | "";
//
//     group  =  service name, "|",
//               sampling priority, "|",
//               sampling mechanism, "|",
//               sampling rate, { "|", future field };
//
//     service name  =  ? unpadded base64 encoded UTF-8 bytes ?;
//
//     (* no plus signs, no exponential notation, etc. *)
//     sampling priority  =  ? decimal integer ? ;
//
//     sampling mechanism  =  ? positive decimal integer ?;
//
//     sampling rate  =  "" | normalized float;
//
//     normalized float  =  ? decimal between 0.0 and 1.0 inclusive with at most four significant
//         digits (rounded up) and with the leading "0." e.g. "0.9877" ?;
//
//     (* somewhat arbitrarily based on the other grammar *)
//     future field  =  ( ? ASCII characters 32-126 ? - delimiter );
//
//     delimiter  =  "|" | ","
//
// That is, semicolon-separated groups of "|"-separated fields.
//
// See `upstream_service_test.cpp` for examples.
//
// [1]: https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form
// [2]:
// https://docs.google.com/document/d/1zeO6LGnvxk5XweObHAwJbK3SfK23z7jQzp7ozWJTa2A/edit#heading=h.2s6f0izi97s1

void appendSamplingRate(std::string& destination, double value) {
  // NaNs serialize as an empty string.
  if (std::isnan(value)) {
    return;
  }

  // `printf`-style formatting rounds if necessary, but we need it to round
  // _up_.  So, use the ceiling function to get the upward rounding behavior,
  // and then format the number.  `double` has enough precision that I don't
  // think this is any different than formatting the number as decimal and
  // then rounding up based on that.
  const double decimal_shift = 1e4;
  const double rounded = std::ceil(value * decimal_shift) / decimal_shift;

  const char* const format = "%1.4f";

  const int formatted_strlen = std::snprintf(nullptr, 0, format, rounded);
  // The +1 is for the null terminator that we will later remove.
  const int buffer_size = formatted_strlen + 1;
  assert(formatted_strlen > 0);
  destination.resize(destination.size() + buffer_size);

  const int rcode =
      std::snprintf(&*(destination.end() - buffer_size), buffer_size, format, rounded);
  (void)rcode;  // Some `assert` macros do this, some don't.
  assert(rcode == formatted_strlen);

  // Remove the null terminator added by `snprintf`.
  destination.pop_back();
}

void appendUpstreamService(std::string& output, const UpstreamService& upstream_service) {
  if (!output.empty()) {
    output += ';';
  }

  appendBase64Unpadded(output, upstream_service.service_name);

  output += '|';
  output += std::to_string(int(upstream_service.sampling_priority));

  output += '|';
  output += std::to_string(upstream_service.sampling_mechanism);

  output += '|';
  appendSamplingRate(output, upstream_service.sampling_rate);
}

}  // namespace opentracing
}  // namespace datadog
