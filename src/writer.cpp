#include "writer.h"
#include <iostream>
#include "encoder.h"
#include "version_number.h"

namespace datadog {
namespace opentracing {

Writer::Writer() : trace_encoder_(std::make_shared<AgentHttpEncoder>()) {}

void ExternalWriter::write(Trace trace) { trace_encoder_->addTrace(std::move(trace)); }

}  // namespace opentracing
}  // namespace datadog
