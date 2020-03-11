#include "writer.h"
#include <iostream>
#include "encoder.h"
#include "span.h"

namespace datadog {
namespace opentracing {

Writer::Writer(std::shared_ptr<RulesSampler> sampler)
    : trace_encoder_(std::make_shared<AgentHttpEncoder>(sampler)) {}

void ExternalWriter::write(Trace trace) { trace_encoder_->addTrace(std::move(trace)); }

}  // namespace opentracing
}  // namespace datadog
