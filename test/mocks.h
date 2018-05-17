#ifndef DD_OPENTRACING_TEST_MOCKS_H
#define DD_OPENTRACING_TEST_MOCKS_H

#include <curl/curl.h>
#include <iostream>
#include <list>
#include <map>
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
  std::unordered_map<std::string, std::string> meta;  // Aka, tags.

  uint64_t traceId() const { return trace_id; }
  uint64_t spanId() const { return span_id; }

  MSGPACK_DEFINE_MAP(name, service, resource, type, start, duration, meta, span_id, trace_id,
                     parent_id, error);

  // Returns a struct that describes the unique information of a span.
  static SpanInfo fromSpan(Span& span) {
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
};

struct MockBuffer : public SpanBuffer<Span> {
  // struct Trace {
  //   std::unique_ptr<std::vector<SpanInfo>> finished_spans;
  //   size_t all_spans;
  // };

  MockBuffer(){};

  void registerSpan(const Span& span) override {
    uint64_t trace_id = span.traceId();
    auto trace = traces.find(trace_id);
    if (trace == traces.end()) {
      traces.emplace(std::make_pair(trace_id, PendingTrace<SpanInfo>{}));
      trace = traces.find(trace_id);
    }
    trace->second.all_spans.insert(span.spanId());
  }

  void finishSpan(Span&& span) override {
    auto trace = traces.find(span.traceId());
    if (trace == traces.end()) {
      std::cerr << "Missing trace for finished span" << std::endl;
    }
    trace->second.finished_spans->emplace_back(SpanInfo::fromSpan(span));
  }

  std::unordered_map<uint64_t, PendingTrace<SpanInfo>> traces;
};

// A Writer implementation that allows access to the Spans recorded.
template <class Span>
struct MockWriter : public Writer<Span> {
  MockWriter() {}
  ~MockWriter() override {}

  void write(Trace<Span> trace) override {
    std::lock_guard<std::mutex> lock_guard{mutex_};
    traces.emplace_back();
    for (auto& span : *trace) {
      traces.back().push_back(span);
    }
  }

  std::vector<std::vector<Span>> traces;

 private:
  mutable std::mutex mutex_;
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
struct MockHandle : public Handle {
  MockHandle() : MockHandle(nullptr) {}
  MockHandle(std::atomic<bool>* is_destructed_) : is_destructed(is_destructed_) {}
  ~MockHandle() override {
    if (is_destructed != nullptr) {
      *is_destructed = true;
    }
  };

  CURLcode setopt(CURLoption key, const char* value) override {
    std::unique_lock<std::mutex> lock(mutex);
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
    std::unique_lock<std::mutex> lock(mutex);
    if (rcode == CURLE_OK) {
      options[key] = std::to_string(value);
    }
    return rcode;
  }

  void setHeaders(std::map<std::string, std::string> headers_) override {
    for (auto& header : headers_) {
      headers[header.first] = header.second;  // Overwrite.
    }
  }

  CURLcode perform() override {
    std::unique_lock<std::mutex> lock(mutex);
    perform_called.notify_all();
    return nextPerformResult();
  }

  // Could be spurious.
  void waitUntilPerformIsCalled() {
    std::unique_lock<std::mutex> lock(mutex);
    perform_called.wait(lock);
  }

  std::string getError() override {
    std::unique_lock<std::mutex> lock(mutex);
    return error;
  }

  // Note, this returns any traces that have been added to the request - NOT traces that have been
  // successfully posted.
  std::unique_ptr<std::vector<std::vector<SpanInfo>>> getTraces() {
    std::unique_lock<std::mutex> lock(mutex);
    std::unique_ptr<std::vector<std::vector<SpanInfo>>> dst{
        new std::vector<std::vector<SpanInfo>>{}};
    if (options.find(CURLOPT_POSTFIELDS) != options.end()) {
      std::string packed_span = options[CURLOPT_POSTFIELDS];
      msgpack::object_handle oh = msgpack::unpack(packed_span.data(), packed_span.size());
      msgpack::object deserialized = oh.get();
      deserialized.convert(*dst.get());
      options.erase(CURLOPT_POSTFIELDS);
    }
    return std::move(dst);
  }

  std::unordered_map<CURLoption, std::string, EnumClassHash> options;
  std::map<std::string, std::string> headers;
  std::string error = "";
  CURLcode rcode = CURLE_OK;
  std::atomic<bool>* is_destructed = nullptr;
  // Each time an perform is called, the next perform_result is used to determine if it
  // succeeds or fails. Loops. Default is for all operations to succeed.
  std::vector<CURLcode> perform_result{CURLE_OK};
  int perform_call_count = 0;

 private:
  // Returns next result code. Expects mutex to be locked already.
  CURLcode nextPerformResult() {
    if (perform_result.size() == 0) {
      return CURLE_OK;
    }
    return perform_result[perform_call_count++ % perform_result.size()];
  }

  std::mutex mutex;
  std::condition_variable perform_called;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TEST_MOCKS_H
