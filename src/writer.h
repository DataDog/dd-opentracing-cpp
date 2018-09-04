#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include "encoder.h"
#include "span.h"
#include "transport.h"

namespace datadog {
namespace opentracing {

// A Writer is used to submit completed traces to the Datadog agent.
class Writer {
 public:
  Writer();

  virtual ~Writer() {}

  // Writes the given Trace.
  virtual void write(Trace trace) = 0;

 protected:
  std::shared_ptr<AgentHttpEncoder> trace_encoder_;
};

// A writer that collects trace data but uses an external mechanism to transmit the data
// to the Datadog Agent.
class ExternalWriter : public Writer {
 public:
  ExternalWriter() {}
  ~ExternalWriter() override {}

  // Implements Writer methods.
  void write(Trace trace) override;

  std::shared_ptr<TraceEncoder> encoder() { return trace_encoder_; }
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
