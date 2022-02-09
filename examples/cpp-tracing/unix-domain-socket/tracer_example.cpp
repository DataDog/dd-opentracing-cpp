#include <datadog/opentracing.h>
#include <datadog/tags.h>

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  datadog::opentracing::TracerOptions options;
  // The environment variable DD_TRACE_AGENT_URL, passed in by docker-compose,
  // will override the "how to connect to the agent" part of `options`.
  auto tracer = datadog::opentracing::makeTracer(options);

  {
    auto span_a = tracer->StartSpan("A");
    span_a->SetTag(datadog::tags::environment, "production");
    span_a->SetTag("tag", 123);
    auto span_b = tracer->StartSpan("B", {opentracing::ChildOf(&span_a->context())});
    span_b->SetTag("tag", "value");
  }

  tracer->Close();
  return 0;
}
