#include "../src/tracer_factory.h"
#include "../src/tracer.h"

// Source file needed to ensure compilation of templated class TracerFactory<MockTracer>
#include "../src/tracer_factory.cpp"

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

// Exists just so we can see that opts was set correctly.
struct MockTracer : public ot::Tracer {
  TracerOptions opts;

  MockTracer(TracerOptions opts_, std::shared_ptr<Writer> &writer,
             std::shared_ptr<SampleProvider> sampler)
      : opts(opts_) {}

  std::unique_ptr<ot::Span> StartSpanWithOptions(ot::string_view operation_name,
                                                 const ot::StartSpanOptions &options) const
      noexcept override {
    return nullptr;
  }

  ot::expected<void> Inject(const ot::SpanContext &sc, std::ostream &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::TextMapWriter &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<void> Inject(const ot::SpanContext &sc,
                            const ot::HTTPHeadersWriter &writer) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(std::istream &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::TextMapReader &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  ot::expected<std::unique_ptr<ot::SpanContext>> Extract(
      const ot::HTTPHeadersReader &reader) const override {
    return ot::make_unexpected(ot::invalid_carrier_error);
  }

  void Close() noexcept override {}
};

TEST_CASE("tracer") {
  TracerFactory<MockTracer> factory;

  SECTION("can be created with valid config") {
    std::string input{R"(
      {
        "service": "my-service",
        "agent_host": "www.omfgdogs.com",
        "agent_port": 80,
        "type": "db"
      }
    )"};
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "");
    REQUIRE(result->get() != nullptr);
    auto tracer = dynamic_cast<MockTracer *>(result->get());
    REQUIRE(tracer->opts.agent_host == "www.omfgdogs.com");
    REQUIRE(tracer->opts.agent_port == 80);
    REQUIRE(tracer->opts.service == "my-service");
    REQUIRE(tracer->opts.type == "db");
  }

  SECTION("can be created without optional fields") {
    std::string input{R"(
      {
        "service": "my-service"
      }
    )"};
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "");
    REQUIRE(result->get() != nullptr);
    auto tracer = dynamic_cast<MockTracer *>(result->get());
    REQUIRE(tracer->opts.agent_host == "localhost");
    REQUIRE(tracer->opts.agent_port == 8126);
    REQUIRE(tracer->opts.service == "my-service");
    REQUIRE(tracer->opts.type == "web");
  }

  SECTION("ignores extra fields") {
    std::string input{R"(
      {
        "service": "my-service",
        "HERP": "DERP"
      }
    )"};
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "");
    REQUIRE(result->get() != nullptr);
    auto tracer = dynamic_cast<MockTracer *>(result->get());
    REQUIRE(tracer->opts.agent_host == "localhost");
    REQUIRE(tracer->opts.agent_port == 8126);
    REQUIRE(tracer->opts.service == "my-service");
    REQUIRE(tracer->opts.type == "web");
  }

  SECTION("has required fields") {
    std::string input{R"(
      {
        "agent_host": "localhost"
      }
    )"};  // Missing service
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "configuration argument 'service' is missing");
    REQUIRE(!result);
    REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
  }

  SECTION("handles invalid JSON") {
    std::string input{R"(
      When I wake up I like a pan of bacon;
      Start off the day with my arteries shakin'!
    )"};
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "configuration is not valid JSON");
    REQUIRE(!result);
    REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
  }

  SECTION("handles bad types") {
    std::string input{R"(
      {
        "service": "my-service",
        "agent_port": "25 year aged tawny"
      }
    )"};
    std::string error = "";
    auto result = factory.MakeTracer(input.c_str(), error);
    REQUIRE(error == "configuration has an argument with an incorrect type");
    REQUIRE(!result);
    REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
  }
}
