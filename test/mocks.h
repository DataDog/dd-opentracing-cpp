#ifndef DD_OPENTRACING_TEST_MOCKS_H
#define DD_OPENTRACING_TEST_MOCKS_H

#include <curl/curl.h>

#include <iostream>
#include <list>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "../src/sample.h"
#include "../src/span.h"
#include "../src/span_buffer.h"
#include "../src/tracer.h"
#include "../src/transport.h"
#include "../src/writer.h"

using dict = std::unordered_map<std::string, std::string>;

namespace std {

// This printing operator is defined here for debugging purposes.
// This way, `REQUIRE(some_dict == another_dict)` will print the values
// when they're not equal.
//
// Doing this is technically [undefined behavior][1], but I can't find a
// compiler that cares.
// [1]: https://en.cppreference.com/w/cpp/language/extending_std
inline std::ostream& operator<<(std::ostream& stream, const dict& map) {
  stream << "unordered_map[";
  auto iter = map.begin();
  const auto end = map.end();
  if (iter != end) {
    stream << iter->first << " = " << iter->second;
    for (++iter; iter != end; ++iter) {
      stream << ", " << iter->first << " = " << iter->second;
    }
  }
  return stream << ']';
}

}  // namespace std

namespace datadog {
namespace opentracing {

// `MockLogger` is an implementation of `Logger` that appends logged records to
// an internal `vector`, `records`.
struct MockLogger : public Logger {
  struct Record {
    LogLevel level;
    uint64_t trace_id;  // zero if absent
    uint64_t span_id;   // zero if absent
    std::string message;
  };

  mutable std::vector<Record> records;

  MockLogger() : Logger([](LogLevel, ot::string_view) {}) {}
  void Log(LogLevel level, ot::string_view message) const noexcept override {
    records.push_back(Record{level, 0, 0, message});
  }
  void Log(LogLevel level, uint64_t trace_id, ot::string_view message) const noexcept override {
    records.push_back(Record{level, trace_id, 0, message});
  }
  void Log(LogLevel level, uint64_t trace_id, uint64_t span_id, ot::string_view message) const
      noexcept override {
    records.push_back(Record{level, trace_id, span_id, message});
  }
  void Trace(ot::string_view message) const noexcept override {
    records.push_back(Record{LogLevel::debug, 0, 0, message});
  }
  void Trace(uint64_t trace_id, ot::string_view message) const noexcept override {
    records.push_back(Record{LogLevel::debug, trace_id, 0, message});
  }
  void Trace(uint64_t trace_id, uint64_t span_id, ot::string_view message) const
      noexcept override {
    records.push_back(Record{LogLevel::debug, trace_id, span_id, message});
  }
};

// Exists just so we can see that opts was set correctly.
struct MockTracer : public Tracer {
  TracerOptions opts;

  MockTracer(TracerOptions opts, std::shared_ptr<Writer> writer,
             std::shared_ptr<RulesSampler> sampler, std::shared_ptr<const Logger> logger)
      : Tracer(opts, writer, sampler, logger), opts(opts) {}

  std::unique_ptr<ot::Span> StartSpanWithOptions(ot::string_view /* operation_name */,
                                                 const ot::StartSpanOptions& /* options */) const
      noexcept override {
    return nullptr;
  }

  // This is here to avoid a warning about hidden overloaded-virtual methods.
  using ot::Tracer::Extract;
  using ot::Tracer::Inject;

  ot::expected<void> Inject(const ot::SpanContext& /* sc */,
                            std::ostream& /* writer */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<void> Inject(const ot::SpanContext& /* sc */,
                            const ot::TextMapWriter& /* writer */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<void> Inject(const ot::SpanContext& /* sc */,
                            const ot::HTTPHeadersWriter& /* writer */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      std::istream& /* reader */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::TextMapReader& /* reader */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::HTTPHeadersReader& /* reader */) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  void Close() noexcept override {}
};

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
                     trace_id, parent_id, error)
};

struct MockPrioritySampler : public PrioritySampler {
  MockPrioritySampler() {}

  SampleResult sample(const std::string& /* environment */, const std::string& /* service */,
                      uint64_t /* trace_id */) const override {
    SampleResult result;
    result.priority_rate = sampling_rate;
    result.sampling_priority = clone(sampling_priority);
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
      result.priority_rate = priority_rate;
      result.applied_rate = applied_rate;
      result.sampling_priority = std::make_unique<SamplingPriority>(*sampling_priority);
      result.sampling_mechanism = sampling_mechanism;
    }
    return result;
  }
  void updatePrioritySampler(json new_config) override { config = new_config.dump(); }

  OptionalSamplingPriority sampling_priority = nullptr;
  OptionalSamplingMechanism sampling_mechanism;
  double rule_rate = 1.0;
  double limiter_rate = 1.0;
  double priority_rate = std::nan("");
  double applied_rate = std::nan("");
  std::string config;
};

// A Writer implementation that allows access to the Spans recorded.
struct MockWriter : public Writer {
  MockWriter(std::shared_ptr<RulesSampler> sampler)
      : Writer(sampler, std::make_shared<MockLogger>()) {}
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

struct MockBuffer : public SpanBuffer {
  MockBuffer()
      : SpanBuffer(std::make_shared<MockLogger>(), nullptr,
                          std::make_shared<RulesSampler>(), SpanBufferOptions{}){};
  explicit MockBuffer(std::shared_ptr<RulesSampler> sampler)
      : SpanBuffer(std::make_shared<MockLogger>(), nullptr, sampler,
                          SpanBufferOptions{}){};
  // This constructor overload is provided for tests where the service name is
  // relevant, such as those involving `PendingTrace::upstream_services`.
  MockBuffer(std::shared_ptr<RulesSampler> sampler, std::string service,
             uint64_t trace_tags_propagation_max_length = 512)
      : SpanBuffer(std::make_shared<MockLogger>(), nullptr, sampler,
                          SpanBufferOptions{true, "localhost", std::nan(""), service,
                                                   trace_tags_propagation_max_length}) {}

  void unbufferAndWriteTrace(uint64_t /* trace_id */) override{
      // Haha NOPE.
      // Leave the trace inside the traces map instead of deleting it.
  };

  std::unordered_map<uint64_t, PendingTrace>& traces() { return traces_; };

  void setEnabled(bool enabled) { options_.enabled = enabled; };

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

  CURLcode setopt(CURLoption key, size_t value) override {
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

// `MockTextMapCarrier` is a `TextMapReader` and a `TextMapWriting` implemented
// in terms of an owned `std::unordered_map<std::string, std::string>`.
// Additionally, it can simulate eventual failure of the `Set` member function
// via the `set_fails_after` data member.
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
