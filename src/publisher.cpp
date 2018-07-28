#include "publisher.h"
#include "span.h"
#include "version_number.h"

namespace datadog {
namespace opentracing {

AgentHttpPublisher::AgentHttpPublisher() {
  // Set up common headers and default encoder
  common_headers_ = {{"Content-Type", "application/msgpack"},
                     {"Datadog-Meta-Lang", "cpp"},
                     {"Datadog-Meta-Tracer-Version", config::tracer_version}};
}

const std::string agent_api_path = "/v0.3/traces";

const std::string AgentHttpPublisher::path() { return agent_api_path; }

void AgentHttpPublisher::clearTraces() { traces_.clear(); }

std::size_t AgentHttpPublisher::pendingTraces() { return traces_.size(); }

const std::map<std::string, std::string> AgentHttpPublisher::headers() {
  std::map<std::string, std::string> headers(common_headers_);
  headers["X-Datadog-Trace-Count"] = std::to_string(traces_.size());
  return headers;
}

const std::string AgentHttpPublisher::payload() {
  buffer_.clear();
  buffer_.str(std::string{});
  msgpack::pack(buffer_, traces_);
  return buffer_.str();
}

void AgentHttpPublisher::addTrace(Trace trace) { traces_.push_back(std::move(trace)); }

}  // namespace opentracing
}  // namespace datadog
