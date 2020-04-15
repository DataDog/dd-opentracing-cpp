#ifndef DD_OPENTRACING_AGENT_WRITER_H
#define DD_OPENTRACING_AGENT_WRITER_H

#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "sample.h"
#include "writer.h"

namespace datadog {
namespace opentracing {

class Handle;

// A Writer that sends Traces (collections of Spans) to a Datadog agent.
class AgentWriter : public Writer {
 public:
  // Creates an AgentWriter that uses curl to send Traces to a Datadog agent. May throw a
  // runtime_exception.
  AgentWriter(std::string host, uint32_t port, std::string unix_socket,
              std::chrono::milliseconds write_period, std::shared_ptr<RulesSampler> sampler);

  AgentWriter(std::unique_ptr<Handle> handle, std::chrono::milliseconds write_period,
              size_t max_queued_traces, std::vector<std::chrono::milliseconds> retry_periods,
              std::string host, uint32_t port, std::string unix_socket,
              std::shared_ptr<RulesSampler> sampler);

  // Does not flush on destruction, buffered traces may be lost. Stops all threads.
  ~AgentWriter() override;

  void write(Trace trace) override;

  // Send all buffered Traces to the destination now. Will block until sending is complete, or
  // timeout passes.
  void flush(std::chrono::milliseconds timeout) override;

  // Permanently stops writing Traces. Calls to write() and flush() will do nothing.
  void stop();

 private:
  // Initialises the curl handle. May throw a runtime_exception.
  void setUpHandle(std::unique_ptr<Handle> &handle, std::string host, uint32_t port,
                   std::string unix_socket);

  // Starts asynchronously writing traces. They will be written periodically (set by write_period_)
  // or when flush() is called manually.
  void startWriting(std::unique_ptr<Handle> handle);
  // Posts the given Traces to the Agent. Returns true if it succeeds, otherwise false.
  static bool postTraces(std::unique_ptr<Handle> &handle,
                         std::map<std::string, std::string> headers, std::string payload);
  // Retries the given function a finite number of times according to retry_periods_. Retries when
  // f() returns false.
  bool retryFiniteOnFail(std::function<bool()> f) const;

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
  mutable std::condition_variable condition_;
  // These two bools, stop_writing_ and flush_worker_, act as signals. They are the predicates on
  // which the condition_ variable acts.
  // If set to true, stops worker. Locked by mutex_;
  bool stop_writing_ = false;
  // If set to true, flushes worker (which sets it false again). Locked by mutex_;
  bool flush_worker_ = false;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_AGENT_WRITER_H
