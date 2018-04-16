#include "transport.h"

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
  curl_slist_free_all(http_headers_);
  curl_easy_cleanup(handle_);
  curl_global_cleanup();
}

CURLcode CurlHandle::setopt(CURLoption key, const char* value) {
  return curl_easy_setopt(handle_, key, value);
}

CURLcode CurlHandle::setopt(CURLoption key, long value) {
  return curl_easy_setopt(handle_, key, value);
}

CURLcode CurlHandle::appendHeaders(std::list<std::string> headers) {
  for (std::string header : headers) {
    http_headers_ = curl_slist_append(http_headers_, header.c_str());
  }
  return curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, http_headers_);
}

CURLcode CurlHandle::perform() { return curl_easy_perform(handle_); };

std::string CurlHandle::getError() { return std::string(curl_error_buffer_); };

}  // namespace opentracing
}  // namespace datadog
