#include "writer.h"

#include <iostream>

#include "encoder.h"
#include "span.h"

namespace datadog {
namespace opentracing {

Writer::Writer(std::shared_ptr<RulesSampler> sampler)
    : trace_encoder_(std::make_shared<AgentHttpEncoder>(sampler)) {}

void ExternalWriter::write(TraceData &trace_data) { trace_encoder_->addTraceData(trace_data); }

}  // namespace opentracing
}  // namespace datadog
