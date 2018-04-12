#include "recorder.h"

namespace datadog {
namespace opentracing {

AgentRecorder::AgentRecorder() {}

AgentRecorder::~AgentRecorder() {}

void AgentRecorder::RecordSpan(Span &&span){};

}  // namespace opentracing
}  // namespace datadog
