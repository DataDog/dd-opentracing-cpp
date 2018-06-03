#include <opentracing/dynamic_load.h>
#include <cstring>
#include <iostream>
#include "tracer.h"
#include "tracer_factory.h"

// Forward declaration of helper functions.
bool equal_or_higher_version(const std::string actual, const std::string min);
bool split_version(const std::string version, int& major, int& minor, int& patch,
                   std::string& label);

int OpenTracingMakeTracerFactory(const char* opentracing_version, const void** error_category,
                                 void** tracer_factory) try {
  /*  if (std::string(opentracing_version) != std::string(OPENTRACING_VERSION)) { */
  if (!equal_or_higher_version(std::string(opentracing_version),
                               std::string(OPENTRACING_VERSION))) {
    std::cerr << "version mismatch: " << std::string(opentracing_version)
              << " != " << std::string(OPENTRACING_VERSION) << std::endl;
    *error_category = static_cast<const void*>(&opentracing::dynamic_load_error_category());
    return opentracing::incompatible_library_versions_error.value();
  }
  *tracer_factory = new datadog::opentracing::TracerFactory<datadog::opentracing::Tracer>{};
  return 0;
} catch (const std::bad_alloc&) {
  *error_category = static_cast<const void*>(&std::generic_category());
  return static_cast<int>(std::errc::not_enough_memory);
}

bool equal_or_higher_version(const std::string actual, const std::string min) {
  if (actual == min) {
    return true;
  }
  // Find major version. They should be equal.
  int amaj = 0;
  int amin = 0;
  int apatch = 0;
  std::string alabel;
  if (!split_version(actual, amaj, amin, apatch, alabel)) {
    return false;
  }
  int mmaj = 0;
  int mmin = 0;
  int mpatch = 0;
  std::string mlabel;
  if (!split_version(min, mmaj, mmin, mpatch, mlabel)) {
    return false;
  }
  if (amaj > mmaj) {
    return false;  // Major version is too high.
  }
  if (amaj == mmaj) {
    if (amin > mmin) {
      return true;  // Higher minor version is acceptable.
    }
    if (amin == mmin) {
      if (apatch > mpatch) {
        return true;  // Higher patch version is acceptable.
      }
      if (apatch == mpatch && alabel == "" || alabel == mlabel) {
        return true;  // Patch levels match. Either no label in actual version, or identical in
                      // both.
      }
    }
  }
  return false;
}

bool split_version(const std::string version, int& major, int& minor, int& patch,
                   std::string& label) try {
  std::string ver = version;
  // Major number.
  auto pos = ver.find(".");
  if (pos == std::string::npos) {
    return false;
  }
  major = std::stoi(ver.substr(0, pos));
  ver = ver.substr(pos + 1);

  // Minor number.
  pos = ver.find(".");
  if (pos == std::string::npos) {
    return false;
  }
  minor = std::stoi(ver.substr(0, pos));
  ver = ver.substr(pos + 1);

  // Patch number.
  pos = ver.find("-");
  if (pos == std::string::npos) {
    patch = std::stoi(ver);
  } else {
    patch = std::stoi(ver.substr(0, pos));
    label = ver.substr(pos + 1);
  }
  return true;
} catch (const std::invalid_argument&) {
  return false;
}
