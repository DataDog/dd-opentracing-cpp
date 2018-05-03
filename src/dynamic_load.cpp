#include <opentracing/dynamic_load.h>
#include <cstring>
#include "tracer.h"
#include "tracer_factory.h"

int OpenTracingMakeTracerFactory(const char *opentracing_version, const void **error_category,
                                 void **tracer_factory) try {
  if (std::string(opentracing_version) != std::string(OPENTRACING_VERSION)) {
    *error_category = static_cast<const void *>(&opentracing::dynamic_load_error_category());
    return opentracing::incompatible_library_versions_error.value();
  }
  *tracer_factory = new datadog::opentracing::TracerFactory<datadog::opentracing::Tracer>{};
  return 0;
} catch (const std::bad_alloc &) {
  *error_category = static_cast<const void *>(&std::generic_category());
  return static_cast<int>(std::errc::not_enough_memory);
}
