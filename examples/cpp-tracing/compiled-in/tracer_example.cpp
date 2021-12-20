#include <datadog/opentracing.h>
#include <datadog/tags.h>
#include <opentracing/propagation.h>

#include <iostream>
#include <string>
#include <unordered_map>

struct HTTPHeadersCarrier : opentracing::HTTPHeadersReader, opentracing::HTTPHeadersWriter {
  HTTPHeadersCarrier(std::unordered_map<std::string, std::string>& text_map_)
      : text_map(text_map_) {}

  opentracing::expected<void> Set(opentracing::string_view key,
                                  opentracing::string_view value) const override {
    text_map[key] = value;
    return {};
  }

  opentracing::expected<void> ForeachKey(
      std::function<opentracing::expected<void>(opentracing::string_view key,
                                                opentracing::string_view value)>
          f) const override {
    for (const auto& key_value : text_map) {
      auto result = f(key_value.first, key_value.second);
      if (!result) return result;
    }
    return {};
  }

  std::unordered_map<std::string, std::string>& text_map;
};

int main(int argc, char* argv[]) {
  datadog::opentracing::TracerOptions tracer_options{"localhost", 8126, "multi-segment-trace"};
  auto tracer = datadog::opentracing::makeTracer(tracer_options);

  // Create some spans.
  {
    auto span_a = tracer->StartSpan("A");
    span_a->SetTag("root", 123);
    std::unordered_map<std::string, std::string> text_map;
    HTTPHeadersCarrier carrier(text_map);
    auto inject_result = tracer->Inject(span_a->context(), carrier);
    if (!inject_result) {
      std::cout << "failed to inject: " << inject_result.error().message() << std::endl;
      return 1;
    }
    // text_map["x-datadog-parent-id"] = "42";
    for (auto& k : text_map) {
      std::cout << k.first << ": " << k.second << std::endl;
    }
    auto extract_result = tracer->Extract(carrier);
    if (!extract_result) {
      std::cout << "failed to extract: " << extract_result.error().message() << std::endl;
      return 1;
    }
    auto span_b = tracer->StartSpan("B", {opentracing::ChildOf(extract_result->get())});
    span_b->SetTag("child-a", "value");
    auto span_b1 = tracer->StartSpan("B1", {opentracing::ChildOf(&span_b->context())});
    span_b1->SetTag("grandchild-a", "value");
    span_b1->Finish();
    span_b->Finish();
    auto span_c = tracer->StartSpan("C", {opentracing::ChildOf(extract_result->get())});
    span_c->SetTag("child-b", "value");
  }

  {
    auto dummy_span = tracer->StartSpan("dummySpan");
    dummy_span->SetTag("error", true);
    dummy_span->Finish();
  }

  tracer->Close();
  return 0;
}
