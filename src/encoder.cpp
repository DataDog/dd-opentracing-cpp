#include "encoder.h"
#include <datadog/version.h>
#include <nlohmann/json.hpp>
#include "sample.h"
#include "span.h"

using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {
const std::string priority_sampling_key = "rate_by_service";
const std::string header_content_type = "Content-Type";
const std::string header_dd_meta_lang = "Datadog-Meta-Lang";
const std::string header_dd_meta_lang_version = "Datadog-Meta-Lang-Version";
const std::string header_dd_meta_tracer_version = "Datadog-Meta-Tracer-Version";
const std::string header_dd_trace_count = "X-Datadog-Trace-Count";

const size_t RESPONSE_ERROR_REGION_SIZE = 50;
}  // namespace

AgentHttpEncoder::AgentHttpEncoder(std::shared_ptr<RulesSampler> sampler) : sampler_(sampler) {
  // Set up common headers and default encoder
  common_headers_ = {{header_content_type, "application/msgpack"},
                     {header_dd_meta_lang, "cpp"},
                     {header_dd_meta_lang_version, ::datadog::version::cpp_version},
                     {header_dd_meta_tracer_version, ::datadog::version::tracer_version}};
}

const std::string agent_api_path = "/v0.4/traces";

const std::string& AgentHttpEncoder::path() { return agent_api_path; }

void AgentHttpEncoder::clearTraces() { traces_.clear(); }

std::size_t AgentHttpEncoder::pendingTraces() { return traces_.size(); }

const std::map<std::string, std::string> AgentHttpEncoder::headers() {
  std::map<std::string, std::string> headers(common_headers_);
  headers[header_dd_trace_count] = std::to_string(traces_.size());
  return headers;
}

const std::string AgentHttpEncoder::payload() {
  buffer_.clear();
  buffer_.str(std::string{});
  msgpack::pack(buffer_, traces_);
  return buffer_.str();
}

void AgentHttpEncoder::addTrace(Trace trace) { traces_.push_back(std::move(trace)); }

void AgentHttpEncoder::handleResponse(const std::string& response) {
  if (sampler_ != nullptr) {
    try {
      json config = json::parse(response);
      if (config.find(priority_sampling_key) == config.end()) {
        return;  // No priority sampling info.
      }
      sampler_->updatePrioritySampler(config[priority_sampling_key]);
    } catch (const json::parse_error& error) {
      size_t start = (error.byte > (RESPONSE_ERROR_REGION_SIZE / 2))
                         ? error.byte - (RESPONSE_ERROR_REGION_SIZE / 2)
                         : 0;
      size_t size = std::min(response.length() - start, RESPONSE_ERROR_REGION_SIZE);
      std::string prefix = (start > 0) ? "..." : "";
      std::string suffix = ((start + size) < response.length()) ? "..." : "";
      std::string response_region = response.substr(start, size);
      std::cerr << "Unable to parse response from agent." << std::endl
                << "Error was: " << error.what() << std::endl
                << "Error near: " << prefix << response_region << suffix << std::endl;
      return;
    }
  }
}

}  // namespace opentracing
}  // namespace datadog
