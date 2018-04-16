#ifndef DD_OPENTRACING_WRITER_H
#define DD_OPENTRACING_WRITER_H

#include "span.h"

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
  AgentWriter();

  ~AgentWriter() override;

  void Write(Span &&span) override;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_WRITER_H
