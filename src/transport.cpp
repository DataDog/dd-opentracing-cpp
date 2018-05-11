#include "transport.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace datadog {
namespace opentracing {

CurlHandle::CurlHandle() {
  curl_global_init(CURL_GLOBAL_ALL);
  handle_ = curl_easy_init();
  // Set the error buffer.
  auto rcode = curl_easy_setopt(handle_, CURLOPT_ERRORBUFFER, curl_error_buffer_);
  if (rcode != CURLE_OK) {
    tearDownHandle();
    throw std::runtime_error(std::string("Unable to set curl error buffer: ") +
                             curl_easy_strerror(rcode));
  }
}

CurlHandle::~CurlHandle() { tearDownHandle(); }

void CurlHandle::tearDownHandle() {
  curl_easy_cleanup(handle_);
  curl_global_cleanup();
}

CURLcode CurlHandle::setopt(CURLoption key, const char* value) {
  return curl_easy_setopt(handle_, key, value);
}

CURLcode CurlHandle::setopt(CURLoption key, long value) {
  return curl_easy_setopt(handle_, key, value);
}

void CurlHandle::setHeaders(std::map<std::string, std::string> headers) {
  for (auto& header : headers) {
    headers_[header.first] = header.second;  // Overwrite.
  }
}

CURLcode CurlHandle::perform() {
  // TODO[willgittoes-dd]: Find a way to not copy these strings each time, without unreasonable
  // coupling to libcurl internals.
  struct curl_slist* http_headers = nullptr;
  for (auto& pair : headers_) {
    std::string header = pair.first + ": " + pair.second;
    http_headers = curl_slist_append(http_headers, header.c_str());
  }
  CURLcode rcode = curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, http_headers);
  if (rcode != CURLE_OK) {
    std::strncpy(curl_error_buffer_, "Unable to write headers", CURL_ERROR_SIZE - 1);
    curl_slist_free_all(http_headers);
    return rcode;
  }
  rcode = curl_easy_perform(handle_);
  curl_slist_free_all(http_headers);
  return rcode;
};

std::string CurlHandle::getError() { return std::string(curl_error_buffer_); };

}  // namespace opentracing
}  // namespace datadog
