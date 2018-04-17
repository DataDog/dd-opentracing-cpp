#include "writer.h"
#include <iostream>
#include "version_number.h"

namespace datadog {
namespace opentracing {

namespace {
const std::string agent_api_path = "/v0.3/traces";
const std::string agent_protocol = "https://";
}  // namespace

template <class Message>
AgentWriter<Message>::AgentWriter(std::string host, uint32_t port)
    : AgentWriter(std::shared_ptr<Handle>{new CurlHandle{}}, config::tracer_version, host, port){};

template <class Message>
AgentWriter<Message>::AgentWriter(std::shared_ptr<Handle> handle, std::string tracer_version,
                                  std::string host, uint32_t port)
    : handle_(std::move(handle)), tracer_version_(tracer_version) {
  setUpHandle(host, port);
}

template <class Message>
void AgentWriter<Message>::setUpHandle(std::string host, uint32_t port) {
  // Some options are the same for all actions, set them here.
  // Set the agent URI.
  std::stringstream agent_uri;
  agent_uri << agent_protocol << host << ":" << port << agent_api_path;
  auto rcode = handle_->setopt(CURLOPT_URL, agent_uri.str().c_str());
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent URL: ") + curl_easy_strerror(rcode));
  }
  // Set the common HTTP headers.
  rcode = handle_->appendHeaders({"Content-Type: application/msgpack", "Datadog-Meta-Lang: cpp",
                                  "Datadog-Meta-Tracer-Version: " + tracer_version_});
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent connection headers: ") +
                             curl_easy_strerror(rcode));
  }
}

template <class Message>
AgentWriter<Message>::~AgentWriter() {}

template <class Message>
void AgentWriter<Message>::write(Message &&message) {
  messages_.push_back(std::move(message));
  flush();  // To be done async in a near-future version.
};

template <class Message>
void AgentWriter<Message>::flush() try {
  // Clear the buffer but keep the allocated memory.
  buffer_.clear();
  buffer_.str(std::string{});
  // Why does the agent want extra nesting?
  std::array<std::reference_wrapper<std::vector<Message>>, 1> messages{messages_};
  msgpack::pack(buffer_, messages);
  std::string post_fields = buffer_.str();

  auto rcode =
      handle_->appendHeaders({"X-Datadog-Trace-Count: " + std::to_string(messages_.size())});
  messages_.clear();
  if (rcode != CURLE_OK) {
    std::cerr << "Error setting agent communication headers: " << curl_easy_strerror(rcode)
              << std::endl;
    return;
  }

  // We have to set the size manually, because msgpack uses null characters.
  rcode = handle_->setopt(CURLOPT_POSTFIELDSIZE, post_fields.size());
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
  // Drop messages, but live to fight another day.
  messages_.clear();
}

// Make sure we generate code for a Span-writing Writer.
template class AgentWriter<Span>;

}  // namespace opentracing
}  // namespace datadog
