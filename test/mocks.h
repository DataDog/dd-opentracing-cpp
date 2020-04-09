#ifndef DD_OPENTRACING_TEST_MOCKS_H
#define DD_OPENTRACING_TEST_MOCKS_H

#include <curl/curl.h>
#include <iostream>
#include <list>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include "../src/sample.h"
#include "../src/span.h"
#include "../src/span_buffer.h"
#include "../src/transport.h"
#include "../src/writer.h"

namespace datadog {
namespace opentracing {

// Allows creation of a SpanData.
struct TestSpanData : public SpanData {
  TestSpanData() {}

  TestSpanData(std::string type, std::string service, ot::string_view resource, std::string name,
               uint64_t trace_id, uint64_t span_id, uint64_t parent_id, int64_t start,
               int64_t duration, int32_t error)
      : SpanData(type, service, resource, name, trace_id, span_id, parent_id, start, duration,
                 error) {}
  TestSpanData(const TestSpanData& other) : SpanData(other) {}

  MSGPACK_DEFINE_MAP(name, service, resource, type, start, duration, meta, metrics, span_id,
                     trace_id, parent_id, error);
};

struct MockPrioritySampler : public PrioritySampler {
  MockPrioritySampler() {}

  SampleResult sample(const std::string& /* environment */, const std::string& /* service */,
                      uint64_t /* trace_id */) const override {
    SampleResult result;
    result.priority_rate = sampling_rate;
    if (sampling_priority != nullptr) {
      result.sampling_priority = std::make_unique<SamplingPriority>(*sampling_priority);
    }
    return result;
  }
  void configure(json new_config) override { config = new_config.dump(); }

  OptionalSamplingPriority sampling_priority = nullptr;
  double sampling_rate;

  std::string config;
};

struct MockRulesSampler : public RulesSampler {
  MockRulesSampler() {}

  SampleResult sample(const std::string& /* environment */, const std::string& /* service */,
                      const std::string& /* name */, uint64_t /* trace_id */) override {
    SampleResult result;
    if (sampling_priority != nullptr) {
      result.rule_rate = rule_rate;
      result.limiter_rate = limiter_rate;
      result.sampling_priority = std::make_unique<SamplingPriority>(*sampling_priority);
    }
    return result;
  }
  void updatePrioritySampler(json new_config) override { config = new_config.dump(); }

  OptionalSamplingPriority sampling_priority = nullptr;
  double rule_rate = 1.0;
  double limiter_rate = 1.0;
  std::string config;
};

// A Writer implementation that allows access to the Spans recorded.
struct MockWriter : public Writer {
  MockWriter(std::shared_ptr<RulesSampler> sampler) : Writer(sampler) {}
  ~MockWriter() override {}

  void write(Trace trace) override {
    std::lock_guard<std::mutex> lock_guard{mutex_};
    traces.emplace_back();
    for (auto& span : *trace) {
      traces.back().push_back(std::move(span));
    }
  }

  void flush(std::chrono::milliseconds /* timeout (unused) */) override{};

  std::vector<std::vector<std::unique_ptr<SpanData>>> traces;

 private:
  mutable std::mutex mutex_;
};

struct MockBuffer : public WritingSpanBuffer {
  MockBuffer()
      : WritingSpanBuffer(nullptr, std::make_shared<RulesSampler>(), WritingSpanBufferOptions{}){};
  MockBuffer(std::shared_ptr<RulesSampler> sampler)
      : WritingSpanBuffer(nullptr, sampler, WritingSpanBufferOptions{}){};

  void unbufferAndWriteTrace(uint64_t /* trace_id */) override{
      // Haha NOPE.
      // Leave the trace inside the traces map instead of deleting it.
  };

  std::unordered_map<uint64_t, PendingTrace>& traces() { return traces_; };

  PendingTrace& traces(uint64_t id) { return traces_[id]; };

  void setHostname(std::string hostname) { options_.hostname = hostname; };

  void setAnalyticsRate(double rate) { options_.analytics_rate = rate; };

  void flush(std::chrono::milliseconds /* timeout (unused) */) override{};
};

// Advances the relative (steady_clock) time in the given TimePoint by the given duration.
// Ignores the absolute/system time.
template <typename Rep, typename Period>
void advanceTime(TimePoint& t, std::chrono::duration<Rep, Period> dur) {
  t.relative_time += dur;
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

  std::string getResponse() override {
    std::unique_lock<std::mutex> lock(mutex);
    return response;
  }

  // Note, this returns any traces that have been added to the request - NOT traces that have been
  // successfully posted.
  std::unique_ptr<std::vector<std::vector<TestSpanData>>> getTraces() {
    std::unique_lock<std::mutex> lock(mutex);
    std::unique_ptr<std::vector<std::vector<TestSpanData>>> dst{
        new std::vector<std::vector<TestSpanData>>{}};
    if (options.find(CURLOPT_POSTFIELDS) != options.end()) {
      std::string packed_span = options[CURLOPT_POSTFIELDS];
      msgpack::object_handle oh = msgpack::unpack(packed_span.data(), packed_span.size());
      msgpack::object deserialized = oh.get();
      deserialized.convert(*dst.get());
      options.erase(CURLOPT_POSTFIELDS);
    }
    return dst;
  }

  std::unordered_map<CURLoption, std::string, EnumClassHash> options;
  std::map<std::string, std::string> headers;
  std::string error = "";
  std::string response = "";
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

// A Mock TextMapReader and TextMapWriter.
// Not in mocks.h since we only need it here for now.
struct MockTextMapCarrier : ot::TextMapReader, ot::TextMapWriter {
  MockTextMapCarrier() {}

  ot::expected<void> Set(ot::string_view key, ot::string_view value) const override {
    if (set_fails_after == 0) {
      return ot::make_unexpected(std::error_code(6, ot::propagation_error_category()));
    } else if (set_fails_after > 0) {
      set_fails_after--;
    }
    text_map[key] = value;
    return {};
  }

  ot::expected<ot::string_view> LookupKey(ot::string_view /* key */) const override {
    return ot::make_unexpected(ot::lookup_key_not_supported_error);
  }

  ot::expected<void> ForeachKey(
      std::function<ot::expected<void>(ot::string_view key, ot::string_view value)> f)
      const override {
    for (const auto& key_value : text_map) {
      auto result = f(key_value.first, key_value.second);
      if (!result) return result;
    }
    return {};
  }

  mutable std::unordered_map<std::string, std::string> text_map;
  // Count-down to method failing. Negative means no failures.
  mutable int set_fails_after = -1;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TEST_MOCKS_H
