#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <curl/curl.h>
#include <sstream>
#include "span.h"
#include "transport.h"

namespace datadog {
namespace opentracing {

class Span;

// Takes Messages and writes them (eg. sends them to Datadog).
template <class Message>
class Writer {
 public:
  Writer() {}

  virtual ~Writer() {}

  // Writes the given span.
  virtual void write(Message &&span) = 0;
};

// A Writer that sends messages to a Datadog agent.
template <class Message>
class AgentWriter : public Writer<Message> {
 public:
  // Creates an AgentWriter that uses curl to send messages to a Datadog agent. May throw a
  // runtime_exception.
  AgentWriter(std::string host, uint32_t port);

  AgentWriter(std::shared_ptr<Handle> handle, std::string tracer_version, std::string host,
              uint32_t port);

  ~AgentWriter() override;

  void write(Message &&span) override;

  // Sends Messages to the agent using a POST request. Doesn't throw exceptions, but may drop
  // messages.
  void flush();

 private:
  // Initialises the curl handle. May throw a runtime_exception.
  void setUpHandle(std::string host, uint32_t port);

  std::shared_ptr<Handle> handle_;
  const std::string tracer_version_;

  // TODO[willgittoes-dd]: Replace this with SPSC sync queue when making this concurrent.
  std::vector<Message> messages_;
  std::stringstream buffer_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
