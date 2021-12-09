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
namespace {

// The following [eBNF][1] grammar describes the upstream services encoding.
// The grammar was copied from [an internal design document][2].
//
//     upstream services  =  group, { ";", group };
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

// Return a `string_view` over the specified range of characters `[begin, end)`.
ot::string_view range(const char* begin, const char* end) {
  assert(begin <= end);
  return ot::string_view{begin, std::size_t(end - begin)};
}

void deserializeServiceName(std::string& destination, ot::string_view text) {
  try {
    base64_rfc4648_unpadded::decode(destination, text);
  } catch (const cppcodec::parse_error& error) {
    throw std::runtime_error(error.what());
  }
}

SamplingPriority deserializeSamplingPriority(ot::string_view text) {
  const std::string sampling_priority_string = text;
  std::size_t bytes_parsed = 0;
  const int sampling_priority_int = std::stoi(sampling_priority_string, &bytes_parsed);
  if (bytes_parsed != sampling_priority_string.size()) {
    std::ostringstream error;
    error << "Unable to parse a sampling priority integer from the following text (consumed "
          << bytes_parsed << " bytes out of " << sampling_priority_string.size()
          << "): " << sampling_priority_string;
    throw std::runtime_error(error.str());
  }
  const OptionalSamplingPriority sampling_priority_maybe =
      asSamplingPriority(sampling_priority_int);
  if (sampling_priority_maybe == nullptr) {
    std::ostringstream error;
    error << "Unrecognized integer value for sampling priority: " << sampling_priority_int;
    throw std::runtime_error(error.str());
  }
  return *sampling_priority_maybe;
}

SamplingMechanism deserializeSamplingMechanism(ot::string_view text) {
  const std::string sampling_mechanism_string = text;
  std::size_t bytes_parsed = 0;
  const int sampling_mechanism_int = std::stoi(sampling_mechanism_string, &bytes_parsed);
  if (bytes_parsed != sampling_mechanism_string.size()) {
    std::ostringstream error;
    error << "Unable to parse a sampling mechanism integer from the following text (consumed "
          << bytes_parsed << " bytes out of " << sampling_mechanism_string.size()
          << "): " << sampling_mechanism_string;
    throw std::runtime_error(error.str());
  }
  return asSamplingMechanism(sampling_mechanism_int);
}

double deserializeSamplingRate(ot::string_view text) {
  if (text.empty()) {
    return std::nan("");
  }

  const std::string sampling_rate_string = text;
  std::size_t bytes_parsed = 0;
  const double sampling_rate = std::stod(sampling_rate_string, &bytes_parsed);
  if (bytes_parsed != sampling_rate_string.size()) {
    std::ostringstream error;
    error << "Unable to parse a sampling rate float from the following text (consumed "
          << bytes_parsed << " bytes out of " << sampling_rate_string.size()
          << "): " << sampling_rate_string;
    throw std::runtime_error(error.str());
  }

  return sampling_rate;
}

// Return an `UpstreamService` parsed from the specified `text`, or throw a
// `std::runtime_error` if an error occurs.
UpstreamService deserialize(ot::string_view text) {
  UpstreamService result;
  const auto end = text.end();

  // .service_name
  auto iter = text.begin();
  auto next = std::find(iter, end, '|');
  deserializeServiceName(result.service_name, range(iter, next));

  // `UpstreamService` fields are "|" separated.  Every time we go to the next
  // field, we have to make sure that there was a "|" before it (otherwise
  // there aren't enough fields).
  const auto advanceTo = [&](const char* field_name_for_diagnostic) {
    if (next == end) {
      std::string error;
      error += "Missing fields in UpstreamService; no ";
      error += field_name_for_diagnostic;
      error += " to decode. Offending text: ";
      error += text;
      throw std::runtime_error(error);
    }
    iter = next + 1;
    next = std::find(iter, end, '|');
  };

  advanceTo("sampling_priority");
  result.sampling_priority = deserializeSamplingPriority(range(iter, next));

  advanceTo("sampling_mechanism");
  result.sampling_mechanism = deserializeSamplingMechanism(range(iter, next));

  advanceTo("sampling_rate");
  result.sampling_rate = deserializeSamplingRate(range(iter, next));

  // Put any remaining fields into `result.unknown_fields`.
  while (next != end) {
    iter = next + 1;
    next = std::find(iter, end, '|');
    result.unknown_fields.push_back(range(iter, next));
  }

  return result;
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

// This function is used in `operator==`, below.
std::string serializeSamplingRate(double value) {
  std::string result;
  appendSamplingRate(result, value);
  return result;
}

}  // namespace

void appendAsBase64Unpadded(std::string& destination, const std::string& source) {
  destination += base64_rfc4648_unpadded::encode(source);
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
  assert(rcode == formatted_strlen);

  // Remove the null terminator added by `snprintf`.
  destination.pop_back();
}

bool operator==(const UpstreamService& left, const UpstreamService& right) {
  // clang-format off
  return left.service_name == right.service_name
      && left.sampling_priority == right.sampling_priority
      && left.sampling_mechanism == right.sampling_mechanism
      // Two things to watch out for with `.sampling_rate`:
      //
      // 1. NaN is used as the N/A value, so we have to work around how NaN !=
      //    NaN.
      // 2. I want `original == deserialize(serialize(original))`.  This
      //    property is handy in the unit test.  It's complicated by the
      //    limited precision used in the formatting of `.sampling_rate`.  
      //
      // We handle both cases by comparing serialized versions of `.sampling_rate`.
      && serializeSamplingRate(left.sampling_rate) == serializeSamplingRate(right.sampling_rate)
      && left.unknown_fields == right.unknown_fields;
  // clang-format on
}

std::vector<UpstreamService> deserializeUpstreamServices(ot::string_view text) {
  std::vector<UpstreamService> result;

  auto iter = text.begin();
  const auto end = text.end();
  while (iter < end) {
    const auto next = std::find(iter, end, ';');
    result.push_back(deserialize(range(iter, next)));
    iter = next + 1;
  }

  return result;
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
