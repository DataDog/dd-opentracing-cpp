#include "upstream_service.h"

#include <cassert>
#include <cmath>
#include <cppcodec/base64_rfc4648.hpp>
#include <string>

namespace datadog {
namespace opentracing {
namespace {

// TODO: document
UpstreamService deserialize(ot::string_view text) {
  // TODO
  (void)text;
  return {};
}

// Append to the specified `output` the serialization of the specified
// `upstream_service`.
void serialize(std::string& output, const UpstreamService& upstream_service) {
  appendAsBase64Unpadded(output, upstream_service.service_name);

  output += '|';
  output += std::to_string(int(upstream_service.sampling_priority));

  output += '|';
  output += std::to_string(asInt(upstream_service.sampling_mechanism));

  output += '|';
  appendSamplingRate(output, upstream_service.sampling_rate);

  for (const std::string& value : upstream_service.unknown_fields) {
    output += '|';
    output += value;
  }
}

}  // namespace

void appendAsBase64Unpadded(std::string& destination, const std::string& source) {
  destination += cppcodec::base64_rfc4648::encode(source);

  // Remove any padding (at the end).
  const std::size_t begin_padding = destination.find_last_not_of('=');
  // Even if `begin_padding` is `std::string::npos`, this still works.
  destination.erase(begin_padding + 1);
}

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
  const double decimal_shift = 10 * 10 * 10 * 10;
  const double rounded = std::ceil(value * decimal_shift) / decimal_shift;

  const char* const format = "%1.4f";

  const int formatted_strlen = std::snprintf(nullptr, 0, format, rounded);
  // The +1" is for the null terminator that we will later remove.
  const int buffer_size = formatted_strlen + 1;
  assert(formatted_strlen > 0);
  destination.resize(destination.size() + buffer_size);

  const int rcode =
      std::snprintf(&*(destination.end() - buffer_size), buffer_size, format, rounded);
  assert(rcode == formatted_strlen);

  // Remove the null terminator added by `snprintf`.
  destination.pop_back();
}

std::vector<UpstreamService> deserializeUpstreamServices(ot::string_view text) {
  // TODO
  (void)text;
  (void)deserialize(text);
  return {};
}

std::string serializeUpstreamServices(const std::vector<UpstreamService>& upstream_services) {
  std::string result;
  if (upstream_services.empty()) {
    return result;
  }

  auto iter = upstream_services.begin();
  serialize(result, *iter);
  for (++iter; iter != upstream_services.end(); ++iter) {
    result += ';';
    serialize(result, *iter);
  }

  return result;
}

}  // namespace opentracing
}  // namespace datadog
