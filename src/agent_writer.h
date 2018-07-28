#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include "publisher.h"
#include "span.h"
#include "transport.h"

namespace datadog {
namespace opentracing {

class SpanData;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

// A Writer is used to submit completed traces to the Datadog agent.
class Writer {
 public:
  Writer();

  virtual ~Writer() {}

  // Writes the given Trace.
  virtual void write(Trace trace) = 0;

 protected:
  std::shared_ptr<AgentHttpPublisher> trace_publisher_;
};

// A Writer that sends Traces (collections of Spans) to a Datadog agent.
class AgentWriter : public Writer {
 public:
  // Creates an AgentWriter that uses curl to send Traces to a Datadog agent. May throw a
  // runtime_exception.
  AgentWriter(std::string host, uint32_t port, std::chrono::milliseconds write_period);

  AgentWriter(std::unique_ptr<Handle> handle, std::chrono::milliseconds write_period,
              size_t max_queued_traces, std::vector<std::chrono::milliseconds> retry_periods,
              std::string host, uint32_t port);

  // Does not flush on destruction, buffered traces may be lost. Stops all threads.
  ~AgentWriter() override;

  void write(Trace trace) override;

  // Send all buffered Traces to the destination now. Will block until sending is complete. This
  // isn't on the main Writer API because real code should not need to call this.
  void flush();

  // Permanently stops writing Traces. Calls to write() and flush() will do nothing.
  void stop();

 private:
  // Initialises the curl handle. May throw a runtime_exception.
  void setUpHandle(std::unique_ptr<Handle> &handle, std::string host, uint32_t port);

  // Starts asynchronously writing traces. They will be written periodically (set by write_period_)
  // or when flush() is called manually.
  void startWriting(std::unique_ptr<Handle> handle);
  // Posts the given Traces to the Agent. Returns true if it succeeds, otherwise false.
  static bool postTraces(std::unique_ptr<Handle> &handle,
                         std::map<std::string, std::string> headers, std::string payload);
  // Retries the given function a finite number of times according to retry_periods_. Retries when
  // f() returns false.
  void retryFiniteOnFail(std::function<bool()> f) const;

  // How often to send Traces.
  const std::chrono::milliseconds write_period_;
  const size_t max_queued_traces_;
  // How long to wait before retrying each time. If empty, only try once.
  const std::vector<std::chrono::milliseconds> retry_periods_;

  // The thread on which traces are encoded and send to the agent. Receives traces on the
  // traces_ queue as notified by condition_. Encodes traces to buffer_ and sends to the
  // agent.
  std::unique_ptr<std::thread> worker_ = nullptr;
  // Locks access to the traces_ queue and the stop_writing_ and flush_worker_ signals.
  mutable std::mutex mutex_;
  // Notifies worker thread when there are new traces in the queue or it should stop.
  std::condition_variable condition_;
  // These two bools, stop_writing_ and flush_worker_, act as signals. They are the predicates on
  // which the condition_ variable acts.
  // If set to true, stops worker. Locked by mutex_;
  bool stop_writing_ = false;
  // If set to true, flushes worker (which sets it false again). Locked by mutex_;
  bool flush_worker_ = false;
};

// A writer that collects trace data but uses an external mechanism to transmit the data
// to the Datadog Agent.
class ExternalWriter : public Writer {
 public:
  ExternalWriter() {}
  ~ExternalWriter() {}

  // Implements Writer methods.
  void write(Trace trace) override;

  std::shared_ptr<TracePublisher> publisher() { return trace_publisher_; }
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
