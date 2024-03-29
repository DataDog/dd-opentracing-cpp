#ifndef DD_OPENTRACING_ENCODER_H
#define DD_OPENTRACING_ENCODER_H

#include <datadog/opentracing.h>

#include <deque>
#include <memory>
#include <sstream>

#include "logger.h"
#include "trace_data.h"

namespace datadog {
namespace opentracing {

class Logger;
class RulesSampler;

class AgentHttpEncoder : public TraceEncoder {
 public:
  AgentHttpEncoder(std::shared_ptr<RulesSampler> sampler, std::shared_ptr<const Logger> logger);
  ~AgentHttpEncoder() override {}

  // Returns the path that is used to submit HTTP requests to the agent.
  const std::string& path() override;
  std::size_t pendingTraces() override;
  void clearTraces() override;
  // Returns the HTTP headers that are required for the collection of traces.
  const std::map<std::string, std::string> headers() override;
  // Returns the encoded payload from the collection of traces.
  const std::string payload() override;
  void handleResponse(const std::string& response) override;
  void addTrace(TraceData trace);

 private:
  // Holds the headers that are used for all HTTP requests.
  std::map<std::string, std::string> common_headers_;
  std::deque<TraceData> traces_;
  std::stringstream buffer_;
  // Responses from the Agent may contain configuration for the sampler. May be nullptr if priority
  // sampling is not enabled.
  std::shared_ptr<RulesSampler> sampler_ = nullptr;
  // The logger is used to print diagnostic messages.  The actual mechanism is
  // determined by the `log_func` field of `TracerOptions`.
  std::shared_ptr<const Logger> logger_;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_ENCODER_H
