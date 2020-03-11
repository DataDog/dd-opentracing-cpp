#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include "encoder.h"

namespace datadog {
namespace opentracing {

class AgentHttpEncoder;
class TraceEncoder;
struct SpanData;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

// A Writer is used to submit completed traces to the Datadog agent.
class Writer {
 public:
  Writer(std::shared_ptr<RulesSampler> sampler);

  virtual ~Writer() {}

  // Writes the given Trace.
  virtual void write(Trace trace) = 0;

  // Send any buffered Traces to the destination now. Will block until sending is complete, or
  // timeout passes.
  virtual void flush(std::chrono::milliseconds timeout) = 0;

 protected:
  std::shared_ptr<AgentHttpEncoder> trace_encoder_;
};

// A writer that collects trace data but uses an external mechanism to transmit the data
// to the Datadog Agent.
class ExternalWriter : public Writer {
 public:
  ExternalWriter(std::shared_ptr<RulesSampler> sampler) : Writer(sampler) {}
  ~ExternalWriter() override {}

  // Implements Writer methods.
  void write(Trace trace) override;

  // No flush implementation, since ExternalWriter is not in charge of its own writing schedule.
  void flush(std::chrono::milliseconds /* timeout (unused) */) override{};

  std::shared_ptr<TraceEncoder> encoder() { return trace_encoder_; }
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
