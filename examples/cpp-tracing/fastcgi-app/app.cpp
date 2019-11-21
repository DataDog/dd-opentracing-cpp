#include <stdlib.h>
#include "fcgiapp.h"

#include <curl/curl.h>

#include <datadog/opentracing.h>
#include <opentracing/tracer.h>
#include <chrono>

// This reader is used to extract parameters from fcgi's environment and produce an
// opentracing SpanContext. This allows linking to existing traces and
// propagating to external services.
struct FCGIParamArrayReader : opentracing::TextMapReader {
  FCGIParamArrayReader(const FCGX_ParamArray& envp) : envp_(envp) {}
  opentracing::expected<void> ForeachKey(
      std::function<opentracing::expected<void>(opentracing::string_view key,
                                                opentracing::string_view value)>
          f) const override {
    auto key_map = std::vector<std::pair<std::string, std::string>>({
        {"HTTP_X_DATADOG_TRACE_ID", "x-datadog-trace-id"},
        {"HTTP_X_DATADOG_PARENT_ID", "x-datadog-parent-id"},
        {"HTTP_X_DATADOG_SAMPLING_PRIORITY", "x-datadog-sampling-priority"},
    });
    for (auto key : key_map) {
      auto value = FCGX_GetParam(key.first.c_str(), envp_);
      f(key.second, value);
    }
    return {};
  }

  FCGX_ParamArray envp_;
};

void setupCurl() { curl_global_init(CURL_GLOBAL_ALL); }

void teardownCurl() { curl_global_cleanup(); }

void setupTracer() {
  // this is the only datadog-specific part, the rest is
  // using only opentracing API calls
  datadog::opentracing::TracerOptions tracer_opts;
  tracer_opts.service = "fastcgi-app";
  auto tracer = datadog::opentracing::makeTracer(tracer_opts);
  opentracing::Tracer::InitGlobal(tracer);
}

std::unique_ptr<opentracing::Span> createSpan(
    const std::chrono::system_clock::time_point& start_time, char** fcgi_envp) {
  // extract propagated context from HTTP headers from fcgi's environment
  auto pctx_maybe = opentracing::Tracer::Global()->Extract(FCGIParamArrayReader(fcgi_envp));
  // apply options
  opentracing::StartSpanOptions span_opts;
  opentracing::StartTimestamp(start_time).Apply(span_opts);
  if (pctx_maybe) {
    opentracing::ChildOf(pctx_maybe->get()).Apply(span_opts);
  }
  return opentracing::Tracer::Global()->StartSpanWithOptions("fastcgi.request", span_opts);
}

struct CurlHeaderWriter : opentracing::TextMapWriter {
  CurlHeaderWriter() : headers(new curl_slist*) { *headers = NULL; }
  opentracing::expected<void> Set(opentracing::string_view key,
                                  opentracing::string_view value) const override {
    std::string header;
    header.append(key);
    header.append(": ");
    header.append(value);
    auto h = curl_slist_append(*headers, header.c_str());
    if (h == NULL) {
      return opentracing::make_unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    *headers = h;
    return {};
  }

  std::unique_ptr<curl_slist*> headers;
};

bool sendCurlRequest(std::unique_ptr<opentracing::Span>& active_span, std::string url) {
  // initialize curl request
  auto curl = curl_easy_init();
  if (!curl) {
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  // inject headers into writer
  CurlHeaderWriter writer;
  active_span->tracer().Inject(active_span->context(), writer);
  // pass headers to curl request
  auto headers = writer.headers.release();
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers);
  // perform the request
  auto res = curl_easy_perform(curl);
  curl_slist_free_all(*headers);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) {
    return false;
  }
  return true;
}

int main(void) {
  int sockfd = FCGX_OpenSocket(":9090", 1024);
  FCGX_Request request;
  FCGX_Init();
  FCGX_InitRequest(&request, sockfd, 0);

  setupCurl();
  setupTracer();

  while (FCGX_Accept_r(&request) >= 0) {
    // capture start time of request
    auto start_time = std::chrono::system_clock::now();
    // other request initialization might need to occur here
    //
    // create the span. when it goes out of scope, it'll be submitted
    auto span = createSpan(start_time, request.envp);

    // perform normal request handling
    // this example sends an HTTP request to a backend component using curl
    auto ok = sendCurlRequest(span, "http://traced-backend/xyz");
    if (!ok) {
      // 5xx error
      FCGX_FPrintF(request.out, "Status: 500 Internal Server Error\r\n\r\n");
      continue;
    }

    FCGX_FPrintF(request.out, "Content-type: text/plain\r\n\r\n");
    // send request to other system
    // Dump envp
    for (char** p = request.envp; *p; ++p) {
      FCGX_FPrintF(request.out, "%s\r\n", *p);
    }
  }
  teardownCurl();
  return 0;
}
