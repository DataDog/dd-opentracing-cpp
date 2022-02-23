#include <datadog/opentracing.h>
#include <datadog/tags.h>
#include <unistd.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  datadog::opentracing::TracerOptions tracer_options{"localhost", 8126, "compiled-in example"};
  auto tracer = datadog::opentracing::makeTracer(tracer_options);

  // Create some spans.
  while(true){
    {
      auto span_a = tracer->StartSpan("A");
      span_a->SetTag(datadog::tags::environment, "production");
      span_a->SetTag("tag", 123);
      auto span_b = tracer->StartSpan("B", {opentracing::ChildOf(&span_a->context())});
      span_b->SetTag("tag", "value");
    }
    tracer->Close();
    sleep(30);
  }
  return 0;
}
