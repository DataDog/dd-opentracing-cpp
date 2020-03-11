#ifndef DD_OPENTRACING_ENCODER_H
#define DD_OPENTRACING_ENCODER_H

#include <datadog/opentracing.h>
#include <deque>
#include <sstream>

namespace datadog {
namespace opentracing {

class RulesSampler;
struct SpanData;
using Trace = std::unique_ptr<std::vector<std::unique_ptr<SpanData>>>;

class AgentHttpEncoder : public TraceEncoder {
 public:
  AgentHttpEncoder(std::shared_ptr<RulesSampler> sampler);
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
  void addTrace(Trace trace);

 private:
  // Holds the headers that are used for all HTTP requests.
  std::map<std::string, std::string> common_headers_;
  std::deque<Trace> traces_;
  std::stringstream buffer_;
  // Responses from the Agent may contain configuration for the sampler. May be nullptr if priority
  // sampling is not enabled.
  std::shared_ptr<RulesSampler> sampler_ = nullptr;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_ENCODER_H
