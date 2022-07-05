#include "../src/glob.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const auto begin_pattern = reinterpret_cast<const char *>(data);
  const auto end = begin_pattern + size;
  for (const char *begin_subject = begin_pattern; begin_subject != end; ++begin_subject) {
    // bool glob_match(ot::string_view pattern, ot::string_view subject);
    datadog::opentracing::glob_match(ot::string_view(begin_pattern, begin_subject - begin_pattern),
                                     ot::string_view(begin_subject, end - begin_subject));
  }
  return 0;
}
