#include "recorder.h"
#include <iostream>
#include <sstream>

namespace datadog {
namespace opentracing {

namespace {
const std::string agent_api_path = "/v0.3/traces";
const std::string agent_protocol = "https://";
}  // namespace

AgentRecorder::AgentRecorder(std::string host, uint32_t port)
    : handle_(std::move(new CurlHandle{})) {
  setUpHandle(host, port);
}

void AgentRecorder::setUpHandle(std::string host, uint32_t port) {
  // Initilaize the CURL handle.

  // Some options are the same for all actions, set them here.
  // Set the agent URI.
  std::stringstream agent_uri;
  agent_uri << agent_protocol << host << ":" << port << agent_api_path;
  auto rcode = handle_->setopt(CURLOPT_URL, agent_uri.str().c_str());
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent URL: ") + curl_easy_strerror(rcode));
  }
  // Set the common HTTP headers.
  rcode =
      handle_->appendHeaders({"X-Datadog-Trace-Count: 1", "Content-Type: application/msgpack"});
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent connection headers: ") +
                             curl_easy_strerror(rcode));
  }
}

AgentRecorder::~AgentRecorder() {}

void AgentRecorder::RecordSpan(Span &&span) {
  spans_.push_back(std::move(span));
  sendSpans();  // To be done async in a near-future version.
};

void AgentRecorder::sendSpans() try {
  // Clear the buffer but keep the allocated memory.
  buffer_.clear();
  buffer_.str(std::string{});
  // Why does the agent want extra nesting?
  std::array<std::reference_wrapper<std::vector<Span>>, 1> spans{spans_};
  msgpack::pack(buffer_, spans);
  std::string post_fields = buffer_.str();

  // We have to set the size manually, because msgpack uses null characters.
  auto rcode = handle_->setopt(CURLOPT_POSTFIELDSIZE, post_fields.size());
  if (rcode != CURLE_OK) {
    std::cerr << "Error setting agent request size: " << curl_easy_strerror(rcode) << std::endl;
    return;
  }

  rcode = handle_->setopt(CURLOPT_POSTFIELDS, post_fields.data());
  if (rcode != CURLE_OK) {
    std::cerr << "Error setting agent request body: " << curl_easy_strerror(rcode) << std::endl;
    return;
  }

  rcode = handle_->perform();
  if (rcode != CURLE_OK) {
    std::cerr << "Error sending traces to agent: " << curl_easy_strerror(rcode) << std::endl
              << handle_->getError() << std::endl;
    return;
  }
} catch (const std::bad_alloc &) {
  // Drop spans, but live to fight another day.
}

}  // namespace opentracing
}  // namespace datadog
