#ifndef DD_OPENTRACING_TRANSPORT_H
#define DD_OPENTRACING_TRANSPORT_H

#include <curl/curl.h>
#include <map>
#include <string>

namespace datadog {
namespace opentracing {

// An interface to a CURL handle. This interface exists to make testing Recorder easier.
class Handle {
 public:
  Handle() {}
  virtual ~Handle() {}
  virtual CURLcode setopt(CURLoption key, const char* value) = 0;
  virtual CURLcode setopt(CURLoption key, long value) = 0;
  virtual void setHeaders(std::map<std::string, std::string> headers) = 0;
  virtual CURLcode perform() = 0;
  virtual std::string getError() = 0;
};

// A Handle that uses real curl to really send things.
class CurlHandle : public Handle {
 public:
  // May throw runtime_error.
  CurlHandle();
  ~CurlHandle() override;
  CURLcode setopt(CURLoption key, const char* value) override;
  CURLcode setopt(CURLoption key, long value) override;
  void setHeaders(std::map<std::string, std::string> headers) override;
  CURLcode perform() override;
  std::string getError() override;

 private:
  // For things that need cleaning up if the constructor fails as well as on destruction.
  void tearDownHandle();

  CURL* handle_;
  // Not unordered, just so that the headers are always in the same order. Makes testing just a bit
  // easier, and the number of headers is so low that the log(n) insert doesn't matter.
  std::map<std::string, std::string> headers_;
  char curl_error_buffer_[CURL_ERROR_SIZE];
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TRANSPORT_H
