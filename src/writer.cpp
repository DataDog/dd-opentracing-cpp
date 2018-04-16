#include "writer.h"

namespace datadog {
namespace opentracing {

AgentWriter::AgentWriter() {}

AgentWriter::~AgentWriter() {}

void AgentWriter::Write(Span &&span){};

}  // namespace opentracing
}  // namespace datadog
