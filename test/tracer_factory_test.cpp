#include "../src/tracer_factory.h"
#include "../src/tracer.h"
#include "mocks.h"

// Source file needed to ensure compilation of templated class TracerFactory<MockTracer>
#include "../src/tracer_factory.cpp"

#include <datadog/tags.h>
#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("tracer factory") {
  TracerFactory<MockTracer> factory;

  SECTION("can create a tracer with valid config") {
    // Checks all combinations.
    auto sampling_rules = GENERATE("[]", "[{\"sample_rate\":0.42}]");
    auto propagation_style_inject =
        GENERATE(values<std::pair<std::string, std::set<PropagationStyle>>>(
            {{"[\"Datadog\"]", {PropagationStyle::Datadog}},
             {"[\"B3\"]", {PropagationStyle::B3}},
             {"[\"Datadog\", \"B3\"]", {PropagationStyle::Datadog, PropagationStyle::B3}},
             {"[\"Datadog\", \"B3\", \"Datadog\", \"B3\"]",
              {PropagationStyle::Datadog, PropagationStyle::B3}}}));
    auto propagation_style_extract =
        GENERATE(values<std::pair<std::string, std::set<PropagationStyle>>>(
            {{"[\"Datadog\"]", {PropagationStyle::Datadog}},
             {"[\"B3\"]", {PropagationStyle::B3}},
             {"[\"Datadog\", \"B3\"]", {PropagationStyle::Datadog, PropagationStyle::B3}},
             {"[\"Datadog\", \"B3\", \"Datadog\", \"B3\"]",
              {PropagationStyle::Datadog, PropagationStyle::B3}}}));
    auto report_hostname = GENERATE(false, true);
    auto analytics_enabled = GENERATE(false, true);
    auto analytics_rate = GENERATE(0.0, 0.5, 1.0);

    std::ostringstream input;
    input << std::boolalpha;
    input << R"(
      {
        "service": "my-service",
        "agent_host": "www.omfgdogs.com",
        "agent_port": 80,
        "type": "db",
        "sampling_rules": )"
          << sampling_rules << R"(,
        "propagation_style_extract": )"
          << propagation_style_extract.first << R"(,
        "propagation_style_inject": )"
          << propagation_style_inject.first << R"(,
        "dd.trace.report-hostname": )"
          << report_hostname << R"(,
        "dd.trace.analytics-enabled": )"
          << analytics_enabled << R"(,
        "dd.trace.analytics-sample-rate": )"
          << analytics_rate << R"(
      }
    )";
    std::string error = "";
    auto result = factory.MakeTracer(input.str().c_str(), error);
    REQUIRE(error == "");
    REQUIRE(result->get() != nullptr);
    auto tracer = dynamic_cast<MockTracer *>(result->get());
    REQUIRE(tracer->opts.agent_host == "www.omfgdogs.com");
    REQUIRE(tracer->opts.agent_port == 80);
    REQUIRE(tracer->opts.service == "my-service");
    REQUIRE(tracer->opts.type == "db");
    REQUIRE(tracer->opts.sampling_rules == sampling_rules);
    REQUIRE(tracer->opts.extract == propagation_style_extract.second);
    REQUIRE(tracer->opts.inject == propagation_style_inject.second);
    REQUIRE(tracer->opts.report_hostname == report_hostname);
    REQUIRE(tracer->opts.analytics_enabled == analytics_enabled);
    REQUIRE(tracer->opts.analytics_rate == analytics_rate);
  }

  SECTION("can create a tracer without optional fields") {
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
    REQUIRE(
        error ==
        "tracer option 'service' has not been set via config or DD_SERVICE environment variable");
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

  SECTION("handles bad propagation style") {
    struct BadValueTest {
      std::string value;
      std::string error_extract;
      std::string error_inject;
    };

    std::string incorrect_type = "configuration has an argument with an incorrect type";
    std::string invalid_value_extract =
        "Invalid value for propagation_style_extract, must be a list of at least one element with "
        "value 'Datadog', or 'B3'";
    std::string invalid_value_inject =
        "Invalid value for propagation_style_inject, must be a list of at least one element with "
        "value 'Datadog', or 'B3'";

    auto bad_value = GENERATE_COPY(values<BadValueTest>(
        {{"\"i dunno\"", incorrect_type, incorrect_type},
         {"[]", invalid_value_extract, invalid_value_inject},
         {"[\"Not a real propagation style!\"]", invalid_value_extract, invalid_value_inject}}));

    SECTION("extract") {
      std::ostringstream input;
      input << R"(
      {
        "service": "my-service",
        "propagation_style_extract": )"
            << bad_value.value << R"(
      })";
      std::string error = "";
      auto result = factory.MakeTracer(input.str().c_str(), error);
      REQUIRE(error == bad_value.error_extract);
      REQUIRE(!result);
      REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
    }

    SECTION("inject") {
      std::ostringstream input;
      input << R"(
      {
        "service": "my-service",
        "propagation_style_inject": )"
            << bad_value.value << R"(
      })";
      std::string error = "";
      auto result = factory.MakeTracer(input.str().c_str(), error);
      REQUIRE(error == bad_value.error_inject);
      REQUIRE(!result);
      REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
    }
  }

  SECTION("injected environment variables") {
    SECTION("DD_ENV overrides default") {
      ::setenv("DD_ENV", "injected-env", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "environment": "env"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_ENV");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.environment == "injected-env");
    }

    SECTION("DD_SERVICE overrides default") {
      ::setenv("DD_SERVICE", "injected-service", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_SERVICE");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.service == "injected-service");
    }

    SECTION("DD_VERSION overrides default") {
      ::setenv("DD_VERSION", "injected-version v0.0.2", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "version": "version v0.0.1"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_VERSION");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.version == "injected-version v0.0.2");
    }

    SECTION("DD_AGENT_HOST overrides default") {
      ::setenv("DD_AGENT_HOST", "injected-hostname", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_AGENT_HOST");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "injected-hostname");
      REQUIRE(tracer->opts.agent_port == 8126);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }

    SECTION("DD_AGENT_HOST overrides configuration") {
      ::setenv("DD_AGENT_HOST", "injected-hostname", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "agent_host": "configured-hostname"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_AGENT_HOST");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "injected-hostname");
      REQUIRE(tracer->opts.agent_port == 8126);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }
    SECTION("empty DD_AGENT_HOST retains default") {
      ::setenv("DD_AGENT_HOST", "", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_AGENT_HOST");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "localhost");
      REQUIRE(tracer->opts.agent_port == 8126);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }

    SECTION("DD_TRACE_AGENT_PORT overrides default") {
      ::setenv("DD_TRACE_AGENT_PORT", "1234", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_PORT");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "localhost");
      REQUIRE(tracer->opts.agent_port == 1234);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }
    SECTION("DD_TRACE_AGENT_PORT overrides configuration") {
      ::setenv("DD_TRACE_AGENT_PORT", "1234", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "agent_port": 8126
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_PORT");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "localhost");
      REQUIRE(tracer->opts.agent_port == 1234);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }
    SECTION("empty DD_TRACE_AGENT_PORT retains default") {
      ::setenv("DD_TRACE_AGENT_PORT", "", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_PORT");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.agent_host == "localhost");
      REQUIRE(tracer->opts.agent_port == 8126);
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.type == "web");
    }
    SECTION("invalid DD_TRACE_AGENT_PORT value returns an error") {
      ::setenv("DD_TRACE_AGENT_PORT", "misconfigured", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_PORT");

      REQUIRE(error == "Value for DD_TRACE_AGENT_PORT is invalid");
      REQUIRE(!result);
      REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
    }

    SECTION("DD_TRACE_SAMPLING_RULES overrides configuration") {
      std::string sampling_rules = R"([{"sample_rate":0.99}])";
      ::setenv("DD_TRACE_SAMPLING_RULES", sampling_rules.c_str(), 0);

      std::string input{R"(
      {
        "service": "my-service",
        "sampling_rules": [{"sample_rate":0.42}]
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_SAMPLING_RULES");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.sampling_rules == sampling_rules);
    }

    SECTION("DD_TRACE_AGENT_URL overrides default") {
      ::setenv("DD_TRACE_AGENT_URL", "/path/to/trace-agent.socket", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_URL");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.agent_url == "/path/to/trace-agent.socket");
    }
    SECTION("DD_TRACE_AGENT_URL overrides configuration") {
      ::setenv("DD_TRACE_AGENT_URL", "/path/to/trace-agent.socket", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "agent_url": "/configured/trace-agent.socket"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_AGENT_URL");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.service == "my-service");
      REQUIRE(tracer->opts.agent_url == "/path/to/trace-agent.socket");
    }

    SECTION("DD_TRACE_REPORT_HOSTNAME overrides default") {
      ::setenv("DD_TRACE_REPORT_HOSTNAME", "true", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_REPORT_HOSTNAME");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.report_hostname);
    }
    SECTION("DD_TRACE_REPORT_HOSTNAME overrides config") {
      ::setenv("DD_TRACE_REPORT_HOSTNAME", "false", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "dd.trace.report-hostname": true
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_REPORT_HOSTNAME");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(!tracer->opts.report_hostname);
    }

    SECTION("DD_TRACE_ANALYTICS_ENABLED overrides default") {
      ::setenv("DD_TRACE_ANALYTICS_ENABLED", "true", 0);
      std::string input{R"(
      {
        "service": "my-service"
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_ANALYTICS_ENABLED");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(tracer->opts.analytics_enabled);
      REQUIRE(tracer->opts.analytics_rate == 1.0);
    }
    SECTION("DD_TRACE_ANALYTICS_ENABLED overrides config") {
      ::setenv("DD_TRACE_ANALYTICS_ENABLED", "false", 0);
      std::string input{R"(
      {
        "service": "my-service",
        "dd.trace.analytics-enabled": true
      }
      )"};
      std::string error = "";
      auto result = factory.MakeTracer(input.c_str(), error);
      ::unsetenv("DD_TRACE_ANALYTICS_ENABLED");

      REQUIRE(error == "");
      REQUIRE(result->get() != nullptr);
      auto tracer = dynamic_cast<MockTracer *>(result->get());
      REQUIRE(!tracer->opts.analytics_enabled);
      REQUIRE(std::isnan(tracer->opts.analytics_rate));
    }
  }
}
