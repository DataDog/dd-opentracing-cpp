#include "writer.h"
#include <iostream>
#include "publisher.h"
#include "version_number.h"

namespace datadog {
namespace opentracing {

Writer::Writer() : trace_publisher_(std::make_shared<AgentHttpPublisher>()) {}

void ExternalWriter::write(Trace trace) { trace_publisher_->addTrace(std::move(trace)); }

}  // namespace opentracing
}  // namespace datadog
