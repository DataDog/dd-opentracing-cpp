#ifndef DD_OPENTRACING_TEST_MOCKS_H
#define DD_OPENTRACING_TEST_MOCKS_H

#include "../src/recorder.h"

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
};

// A Recorder implemenentation that allows access to the Spans recorded.
struct MockRecorder : public Recorder {
  MockRecorder() {}
  ~MockRecorder() override {}

  void RecordSpan(Span &&span) override { spans.push_back(MockRecorder::getSpanInfo(span)); }

  // Returns a struct that describes the unique information of a span.
  static SpanInfo getSpanInfo(Span &span) {
    return SpanInfo{span.name,     span.service,   span.resource, span.type,  span.span_id,
                    span.trace_id, span.parent_id, span.error,    span.start, span.duration};
  }

  std::vector<SpanInfo> spans;
};

// Advances the relative (steady_clock) time in the given TimePoint by the given number of seconds.
// Ignores the absolute/system time.
void advanceSeconds(TimePoint &t, int s) {
  std::chrono::duration<int, std::ratio<1>> by(s);
  t.relative_time += by;
}

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_TEST_MOCKS_H
