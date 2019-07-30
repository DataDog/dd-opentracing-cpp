#include <opentracing/dynamic_load.h>
#include <opentracing/tracer.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  // Load the tracer library.
  std::string error_message;
  auto handle_maybe = opentracing::DynamicallyLoadTracingLibrary(
      "/usr/local/lib/libdd_opentracing_plugin.so", error_message);
  if (!handle_maybe) {
    std::cerr << "Failed to load tracer library " << error_message << "\n";
    return 1;
  }

  // Read in the tracer's configuration.
  std::string tracer_config = R"({
      "service": "dynamic-load example",
      "agent_host": "dd-agent",
      "agent_port": 8126
    })";

  // Construct a tracer.
  auto& tracer_factory = handle_maybe->tracer_factory();
  auto tracer_maybe = tracer_factory.MakeTracer(tracer_config.c_str(), error_message);
  if (!tracer_maybe) {
    std::cerr << "Failed to create tracer " << error_message << "\n";
    return 1;
  }
  // Keep the original tracer. We reset to it at the end.
  auto original_tracer = opentracing::Tracer::Global();
  // Initialize the global tracer.
  opentracing::Tracer::InitGlobal(*tracer_maybe);

  // Create some spans.
  {
    // Fetch the global tracer
    auto globalTracer = opentracing::Tracer::Global();
    const char* exchange = "adx";
    auto span = globalTracer->StartSpan("test_span");
    span->SetTag("exchange", exchange);
    char badly_terminated[4];
    strncpy(badly_terminated, "test", 4);
    span->SetTag("tag", badly_terminated);
    auto span_a = globalTracer->StartSpan("A");
    span_a->SetTag("tag", 123);
    auto span_b = globalTracer->StartSpan("B", {opentracing::ChildOf(&span_a->context())});
  }

  // Close the current tracer as a courtesy.
  opentracing::Tracer::Global()->Close();
  // Reset to the original tracer.
  opentracing::Tracer::InitGlobal(original_tracer);
  return 0;
}
