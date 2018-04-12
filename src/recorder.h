#ifndef DD_OPENTRACING_RECORDER_H
#define DD_OPENTRACING_RECORDER_H

#include "span.h"

namespace datadog {
namespace opentracing {

class Span;

// Takes Spans and records them (eg. sends them to Datadog).
class Recorder {
 public:
  Recorder() {}

  virtual ~Recorder() {}

  // Records the given span.
  virtual void RecordSpan(Span &&span) = 0;
};

// A Recorder that sends spans to a Datadog agent.
class AgentRecorder : public Recorder {
 public:
  AgentRecorder();

  ~AgentRecorder() override;

  void RecordSpan(Span &&span) override;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_RECORDER_H
