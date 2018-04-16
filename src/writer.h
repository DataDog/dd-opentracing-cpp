#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include <curl/curl.h>
#include <sstream>
#include "span.h"
#include "transport.h"

namespace datadog {
namespace opentracing {

class Span;

// Takes Spans and records them (eg. sends them to Datadog).
class Writer {
 public:
  Writer() {}

  virtual ~Writer() {}

  // Writes the given span.
  virtual void Write(Span &&span) = 0;
};

// A Writer that sends spans to a Datadog agent.
class AgentWriter : public Writer {
 public:
  // Creates an AgentWriter that uses curl to send spans to a Datadog agent. May throw a
  // runtime_exception.
  AgentWriter(std::string host, uint32_t port);

  ~AgentWriter() override;

  void Write(Span &&span) override;

 private:
  // Initialises the curl handle. May throw a runtime_exception.
  void setUpHandle(std::string host, uint32_t port);
  // Sends Spans to the agent using a POST request. Doesn't throw exceptions, but may drop spans.
  void sendSpans();

  std::unique_ptr<Handle> handle_;

  // TODO[willgittoes-dd]: Replace this with SPSC sync queue when making this concurrent.
  std::vector<Span> spans_;
  std::stringstream buffer_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
