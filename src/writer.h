#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include "span.h"
#include "transport.h"

namespace datadog {
namespace opentracing {

class Span;

// Takes Spans and writes them (eg. sends them to Datadog).
template <class Span>
class Writer {
 public:
  Writer() {}

  virtual ~Writer() {}

  // Writes the given Span.
  virtual void write(Span &&span) = 0;
};

// A Writer that sends spans to a Datadog agent.
template <class Span>
class AgentWriter : public Writer<Span> {
 public:
  // Creates an AgentWriter that uses curl to send spans to a Datadog agent. May throw a
  // runtime_exception.
  AgentWriter(std::string host, uint32_t port);

  AgentWriter(std::unique_ptr<Handle> handle, std::string tracer_version,
              std::chrono::milliseconds write_period, std::string host, uint32_t port);

  // Does not flush on destruction, buffered spans may be lost. Stops all threads.
  ~AgentWriter() override;

  void write(Span &&span) override;

  // Send all buffered Spans to the destination now. Will block until sending is complete. This
  // isn't on the main Writer API because real code should not need to call this.
  void flush();

  // Permanently stops writing Spans. Calls to write() and flush() will do nothing.
  void stop();

 private:
  // Initialises the curl handle. May throw a runtime_exception.
  void setUpHandle(std::unique_ptr<Handle> &handle, std::string host, uint32_t port);

  // Starts asynchronously writing spans. They will be written periodically (set by write_period_)
  // or when flush() is called manually.
  void startWriting(std::unique_ptr<Handle> handle);
  static void postSpans(std::unique_ptr<Handle> &handle, std::stringstream &buffer,
                        size_t num_spans);

  const std::string tracer_version_;
  // How often to send Spans.
  const std::chrono::milliseconds write_period_;

  // The thread on which spans are encoded and send to the agent. Receives spans on the
  // spans_ queue as notified by condition_. Encodes spans to buffer_ and sends to the
  // agent.
  std::unique_ptr<std::thread> worker_ = nullptr;
  // These two bools, stop_writing_ and flush_worker_, act as signals. They are the predicates on
  // which the condition_ variable acts.
  // If set to true, stops worker. Locked by mutex_;
  bool stop_writing_ = false;
  // If set to true, flushes worker (which sets it false again). Locked by mutex_;
  bool flush_worker_ = false;
  // Locks access to the spans_ queue and the stop_writing_ and flush_worker_ signals.
  std::mutex mutex_;
  // Notifies worker thread when there are new spans in the queue or it should stop.
  std::condition_variable condition_;
  // Multiple producer (potentially), single consumer. Locked by mutex_.
  std::deque<Span> spans_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
