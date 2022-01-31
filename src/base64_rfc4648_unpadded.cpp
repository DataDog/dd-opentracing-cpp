#include "base64_rfc4648_unpadded.h"

#include <cassert>
#include <cstddef>

// This encoder is based on
// https://raw.githubusercontent.com/DataDog/driveline/500474309e6150cd79fdf6504b27fd015edab1ce/src/common/base64.h

namespace datadog {
namespace opentracing {
namespace {

size_t base64_expected_output_len(size_t input_len, bool padding) {
  const size_t last_quantum = (input_len / 3) * 3;
  const size_t last_quantum_size = input_len - last_quantum;
  const size_t expected_dst_len =
      (last_quantum * 4 / 3) + (last_quantum_size == 0 ? 0 : last_quantum_size + 1);
  return padding ? (expected_dst_len + 3) & ~3 : expected_dst_len;
}

void base64_generic(const char *b64, const unsigned char *src, size_t src_len, char *dst,
                    size_t dst_len, bool padding) {
  assert(dst_len >= base64_expected_output_len(src_len, padding));
  (void)dst_len;

  // Round up to the nearest multiple of 4 for the dst length (with padding).
  const size_t last_quantum = (src_len / 3) * 3;
  for (size_t i = 0; i < last_quantum; i += 3) {
    *dst++ = b64[(src[i] >> 2) & 63];
    *dst++ = b64[((src[i] & 3) << 4) | ((src[i + 1] & 240) >> 4)];
    *dst++ = b64[((src[i + 1] & 15) << 2) | ((src[i + 2] & 192) >> 6)];
    *dst++ = b64[src[i + 2] & 63];
  }
  switch (src_len - last_quantum) {
    case 2:
      *dst++ = b64[(src[last_quantum] >> 2) & 63];
      *dst++ = b64[((src[last_quantum] & 3) << 4) | ((src[last_quantum + 1] & 240) >> 4)];
      *dst++ = b64[((src[last_quantum + 1] & 15) << 2)];
      if (!padding) {
        break;
      }
      *dst++ = '=';
      break;
    case 1:
      *dst++ = b64[(src[last_quantum] >> 2) & 63];
      *dst++ = b64[(src[last_quantum] & 3) << 4];
      if (!padding) {
        break;
      }
      *dst++ = '=';
      *dst++ = '=';
      break;
  }
}

void base64(const unsigned char *src, size_t src_len, char *dst, size_t dst_len,
            bool padding = true) {
  static const char rfc4648_charset[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  base64_generic(rfc4648_charset, src, src_len, dst, dst_len, padding);
}

}  // namespace

void appendBase64Unpadded(std::string &destination, ot::string_view source) {
  const bool padding = false;
  const auto base64_length = base64_expected_output_len(source.size(), padding);
  destination.resize(destination.size() + base64_length);
  base64(reinterpret_cast<const unsigned char *>(source.data()), source.size(),
         &*(destination.end() - base64_length), base64_length, padding);
}

}  // namespace opentracing
}  // namespace datadog
