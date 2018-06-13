#include <stdexcept>

#include "version_check.h"

namespace datadog {
namespace opentracing {

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

  // Patch number, and possible trailing label.
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
      if (apatch == mpatch) {
        if (alabel == "" || alabel == mlabel) {
          return true;  // Patch levels match. Either no label in actual version, or identical in
          // both.
        }
      }
    }
  }
  return false;
}

}  // namespace opentracing
}  // namespace datadog
