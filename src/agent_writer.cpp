#include "writer.h"
#include <iostream>
#include "publisher.h"
#include "version_number.h"

namespace datadog {
namespace opentracing {

Writer::Writer() : trace_publisher_(std::make_shared<AgentHttpPublisher>()) {}

namespace {
const std::string agent_protocol = "http://";
const size_t max_queued_traces = 7000;
// Retry sending traces to agent a couple of times. Any more than that and the agent won't accept
// them.
// write_period 1s + timeout 2s + (retry & timeout) 2.5s + (retry and timeout) 4.5s = 10s.
const std::vector<std::chrono::milliseconds> default_retry_periods{
    std::chrono::milliseconds(500), std::chrono::milliseconds(2500)};
// Agent communication timeout.
const long default_timeout_ms = 2000L;
}  // namespace

AgentWriter::AgentWriter(std::string host, uint32_t port, std::chrono::milliseconds write_period)
    : AgentWriter(std::unique_ptr<Handle>{new CurlHandle{}}, write_period, max_queued_traces,
                  default_retry_periods, host, port){};

AgentWriter::AgentWriter(std::unique_ptr<Handle> handle, std::chrono::milliseconds write_period,
                         size_t max_queued_traces,
                         std::vector<std::chrono::milliseconds> retry_periods, std::string host,
                         uint32_t port)
    : write_period_(write_period),
      max_queued_traces_(max_queued_traces),
      retry_periods_(retry_periods) {
  setUpHandle(handle, host, port);
  startWriting(std::move(handle));
}

void AgentWriter::setUpHandle(std::unique_ptr<Handle> &handle, std::string host, uint32_t port) {
  // Some options are the same for all actions, set them here.
  // Set the agent URI.
  std::stringstream agent_uri;
  agent_uri << agent_protocol << host << ":" << std::to_string(port) << trace_publisher_->path();
  auto rcode = handle->setopt(CURLOPT_URL, agent_uri.str().c_str());
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent URL: ") + curl_easy_strerror(rcode));
  }
  rcode = handle->setopt(CURLOPT_TIMEOUT_MS, default_timeout_ms);
  if (rcode != CURLE_OK) {
    throw std::runtime_error(std::string("Unable to set agent timeout: ") +
                             curl_easy_strerror(rcode));
  }
}  // namespace opentracing

AgentWriter::~AgentWriter() { stop(); }

void AgentWriter::stop() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stop_writing_) {
      return;  // Already stopped.
    }
    stop_writing_ = true;
  }
  condition_.notify_all();
  worker_->join();
}

void AgentWriter::write(Trace trace) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (stop_writing_) {
    return;
  }
  if (trace_publisher_->pendingTraces() >= max_queued_traces_) {
    return;
  }
  trace_publisher_->addTrace(std::move(trace));
};

void AgentWriter::startWriting(std::unique_ptr<Handle> handle) {
  // Start worker that sends Traces to agent.
  // We can capture 'this' because destruction of this stops the thread and the lambda.
  worker_ = std::make_unique<std::thread>(
      [this](std::unique_ptr<Handle> handle) {
        size_t num_traces = 0;
        std::map<std::string, std::string> headers;
        std::string payload;
        while (true) {
          // Encode traces when there are new ones.
          {
            // Wait to be told about new traces (or to stop).
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait_for(lock, write_period_,
                                [&]() -> bool { return flush_worker_ || stop_writing_; });
            if (stop_writing_) {
              return;  // Stop the thread.
            }
            num_traces = trace_publisher_->pendingTraces();
            if (num_traces == 0) {
              continue;
            }
            headers = trace_publisher_->headers();
            payload = trace_publisher_->payload();
            trace_publisher_->clearTraces();
          }  // lock on mutex_ ends.
          // Send spans, not in critical period.
          retryFiniteOnFail([&]() { return AgentWriter::postTraces(handle, headers, payload); });
          // Let thread calling 'flush' that we're done flushing.
          {
            std::unique_lock<std::mutex> lock(mutex_);
            flush_worker_ = false;
          }
          condition_.notify_all();
        }
      },
      std::move(handle));
}

void AgentWriter::flush() try {
  std::unique_lock<std::mutex> lock(mutex_);
  flush_worker_ = true;
  condition_.notify_all();
  // Wait until flush is complete.
  condition_.wait(lock, [&]() -> bool { return !flush_worker_ || stop_writing_; });
} catch (const std::bad_alloc &) {
}

void AgentWriter::retryFiniteOnFail(std::function<bool()> f) const {
  for (std::chrono::milliseconds backoff : retry_periods_) {
    if (f()) {
      return;
    }
    {
      // Just check for stop_writing_ between attempts. No need to allow wake-from-sleep
      // stop_writing signal during retry period since that should always be short.
      std::unique_lock<std::mutex> lock(mutex_);
      if (stop_writing_) {
        return;
      }
    }
    std::this_thread::sleep_for(backoff);
  }
  f();  // Final try after final sleep.
}

bool AgentWriter::postTraces(std::unique_ptr<Handle> &handle,
                             std::map<std::string, std::string> headers, std::string payload) try {
  handle->setHeaders(headers);

  // We have to set the size manually, because msgpack uses null characters.
  CURLcode rcode = handle->setopt(CURLOPT_POSTFIELDSIZE, payload.size());
  if (rcode != CURLE_OK) {
    std::cerr << "Error setting agent request size: " << curl_easy_strerror(rcode) << std::endl;
    return false;
  }

  rcode = handle->setopt(CURLOPT_POSTFIELDS, payload.data());
  if (rcode != CURLE_OK) {
    std::cerr << "Error setting agent request body: " << curl_easy_strerror(rcode) << std::endl;
    return false;
  }

  rcode = handle->perform();
  if (rcode != CURLE_OK) {
    std::cerr << "Error sending traces to agent: " << curl_easy_strerror(rcode) << std::endl
              << handle->getError() << std::endl;
    return false;
  }
  return true;
} catch (const std::bad_alloc &) {
  // Drop spans, but live to fight another day.
  return true;  // Don't attempt to retry.
}

void ExternalWriter::write(Trace trace) { trace_publisher_->addTrace(std::move(trace)); }

}  // namespace opentracing
}  // namespace datadog
