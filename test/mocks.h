#ifndef DD_OPENTRACING_TEST_MOCKS_H
#define DD_OPENTRACING_TEST_MOCKS_H

#include <curl/curl.h>
#include <list>
#include <sstream>
#include <unordered_map>
#include "../src/writer.h"

namespace datadog {
namespace opentracing {

// Simply encapsulates the unique information about a Span.
struct SpanInfo {
  std::string name;
  std::string service;
  std::string resource;
  std::string type;
  uint64_t span_id;
  uint64_t trace_id;
  uint64_t parent_id;
  int32_t error;
  int64_t start;
  int64_t duration;

  MSGPACK_DEFINE_MAP(name, service, resource, type, start, duration, span_id, trace_id, parent_id,
                     error);
};

// A Writer implemenentation that allows access to the Spans recorded.
struct MockWriter : public Writer<Span> {
  MockWriter() {}
  ~MockWriter() override {}

  void write(Span&& span) override { spans.push_back(MockWriter::getSpanInfo(span)); }

  // Returns a struct that describes the unique information of a span.
  static SpanInfo getSpanInfo(Span& span) {
    // Encode Span into msgpack and decode into SpanInfo.
    std::stringstream buffer;
    msgpack::pack(buffer, span);
    // Decode.
    buffer.seekg(0);
    std::string str(buffer.str());
    msgpack::object_handle oh = msgpack::unpack(str.data(), str.size());
    msgpack::object deserialized = oh.get();
    SpanInfo dst;
    deserialized.convert(dst);
    return dst;
  }

  std::vector<SpanInfo> spans;
};

// Advances the relative (steady_clock) time in the given TimePoint by the given number of seconds.
// Ignores the absolute/system time.
void advanceSeconds(TimePoint& t, int s) {
  std::chrono::duration<int, std::ratio<1>> by(s);
  t.relative_time += by;
}

// Enums not hashable on some recent GCC versions:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=60970
struct EnumClassHash {
  template <typename T>
  std::size_t operator()(T t) const {
    return static_cast<std::size_t>(t);
  }
};

// A Handle that doesn't actually make network requests.
class MockHandle : public Handle {
 public:
  MockHandle() {}
  ~MockHandle() override{};

  CURLcode setopt(CURLoption key, const char* value) override {
    if (rcode == CURLE_OK) {
      // We might have null characters if it's the POST data, thanks msgpack!
      if (key == CURLOPT_POSTFIELDS && options.find(CURLOPT_POSTFIELDSIZE) != options.end()) {
        long len = std::stol(options.find(CURLOPT_POSTFIELDSIZE)->second);
        options[key] = std::string(value, len);
      } else {
        options[key] = std::string(value);
      }
    }
    return rcode;
  }

  CURLcode setopt(CURLoption key, long value) override {
    if (rcode == CURLE_OK) {
      options[key] = std::to_string(value);
    }
    return rcode;
  }

  CURLcode appendHeaders(std::list<std::string> new_headers) override {
    if (rcode == CURLE_OK) {
      headers.insert(headers.end(), new_headers.begin(), new_headers.end());
    }
    return rcode;
  }

  CURLcode perform() override { return rcode; }

  std::string getError() override { return error; }

  std::unique_ptr<std::vector<std::vector<SpanInfo>>> getSpans() {
    std::string packed_span = options[CURLOPT_POSTFIELDS];
    msgpack::object_handle oh = msgpack::unpack(packed_span.data(), packed_span.size());
    msgpack::object deserialized = oh.get();
    std::unique_ptr<std::vector<std::vector<SpanInfo>>> dst{
        new std::vector<std::vector<SpanInfo>>{}};
    deserialized.convert(*dst.get());
    return std::move(dst);
  }

  std::unordered_map<CURLoption, std::string, EnumClassHash> options;
  std::list<std::string> headers;
  std::string error = "";
  CURLcode rcode = CURLE_OK;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TEST_MOCKS_H
