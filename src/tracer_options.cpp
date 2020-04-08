#include "tracer_options.h"

#include <iterator>
#include <sstream>

#include <datadog/tags.h>
#include <opentracing/ext/tags.h>

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

// Extracts key-value pairs from a string.
// Duplicates are overwritten. Empty keys are ignored.
// Intended use is for settings tags from DD_TAGS options.
std::map<std::string, std::string> keyvalues(std::string text, char itemsep, char tokensep,
                                             char escape) {
  // early-return if empty
  if (text.empty()) {
    return {};
  }

  bool esc = false;
  std::map<std::string, std::string> kvp;
  std::string key;
  std::string val;
  bool keyfound = false;
  auto assignchar = [&](char c) {
    if (keyfound) {
      val += c;
    } else {
      key += c;
    }
    esc = false;
  };
  auto addkv = [&](std::string key, std::string val) {
    if (key.empty()) {
      return;
    }
    if (val.empty()) {
      return;
    }
    kvp[key] = val;
  };
  for (auto ch : text) {
    if (esc) {
      assignchar(ch);
      continue;
    }
    if (ch == escape) {
      esc = true;
      continue;
    }
    if (ch == tokensep) {
      addkv(key, val);
      key = "";
      val = "";
      keyfound = false;
      continue;
    }
    if (ch == itemsep) {
      keyfound = true;
      continue;
    }
    assignchar(ch);
  }
  if (!key.empty()) {
    addkv(key, val);
  }

  return kvp;
}

ot::expected<TracerOptions, const char *> applyTracerOptionsFromEnvironment(
    const TracerOptions &input) {
  TracerOptions opts = input;

  auto environment = std::getenv("DD_ENV");
  if (environment != nullptr && std::strlen(environment) > 0) {
    opts.environment = environment;
  }

  auto service = std::getenv("DD_SERVICE");
  if (service != nullptr && std::strlen(service) > 0) {
    opts.service = service;
  }

  auto version = std::getenv("DD_VERSION");
  if (version != nullptr && std::strlen(version) > 0) {
    opts.version = version;
  }

  auto tags = std::getenv("DD_TAGS");
  if (tags != nullptr && std::strlen(tags) > 0) {
    opts.tags = keyvalues(tags, ':', ',', '\\');
    // Special cases for env, version and sampling priority
    if (environment != nullptr && std::strlen(environment) > 0 &&
        opts.tags.find(datadog::tags::environment) != opts.tags.end()) {
      opts.tags.erase(datadog::tags::environment);
    }
    if (version != nullptr && std::strlen(version) > 0 &&
        opts.tags.find(datadog::tags::version) != opts.tags.end()) {
      opts.tags.erase(datadog::tags::version);
    }
    if (opts.tags.find(ot::ext::sampling_priority) != opts.tags.end()) {
      opts.tags.erase(ot::ext::sampling_priority);
    }
  }

  auto agent_host = std::getenv("DD_AGENT_HOST");
  if (agent_host != nullptr && std::strlen(agent_host) > 0) {
    opts.agent_host = agent_host;
  }

  auto trace_agent_port = std::getenv("DD_TRACE_AGENT_PORT");
  if (trace_agent_port != nullptr && std::strlen(trace_agent_port) > 0) {
    try {
      opts.agent_port = std::stoi(trace_agent_port);
    } catch (const std::invalid_argument &ia) {
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is invalid");
    } catch (const std::out_of_range &oor) {
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is out of range");
    }
  }

  auto sampling_rules = std::getenv("DD_TRACE_SAMPLING_RULES");
  if (sampling_rules != nullptr && std::strlen(sampling_rules) > 0) {
    opts.sampling_rules = sampling_rules;
  }

  auto trace_agent_url = std::getenv("DD_TRACE_AGENT_URL");
  if (trace_agent_url != nullptr && std::strlen(trace_agent_url) > 0) {
    opts.agent_url = trace_agent_url;
  }

  auto extract = std::getenv("DD_PROPAGATION_STYLE_EXTRACT");
  if (extract != nullptr && std::strlen(extract) > 0) {
    std::stringstream words{extract};
    auto style_maybe = asPropagationStyle(std::vector<std::string>{
        std::istream_iterator<std::string>{words}, std::istream_iterator<std::string>{}});
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_EXTRACT is invalid");
    }
    opts.extract = style_maybe.value();
  }

  auto inject = std::getenv("DD_PROPAGATION_STYLE_INJECT");
  if (inject != nullptr && std::strlen(inject) > 0) {
    std::stringstream words{inject};
    auto style_maybe = asPropagationStyle(std::vector<std::string>{
        std::istream_iterator<std::string>{words}, std::istream_iterator<std::string>{}});
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_INJECT is invalid");
    }
    opts.inject = style_maybe.value();
  }

  auto report_hostname = std::getenv("DD_TRACE_REPORT_HOSTNAME");
  if (report_hostname != nullptr) {
    auto value = std::string(report_hostname);
    if (value == "true" || value == "1") {
      opts.report_hostname = true;
    } else if (value == "false" || value == "0" || value == "") {
      opts.report_hostname = false;
    } else {
      return ot::make_unexpected("Value for DD_TRACE_REPORT_HOSTNAME is invalid");
    }
  }

  auto analytics_enabled = std::getenv("DD_TRACE_ANALYTICS_ENABLED");
  if (analytics_enabled != nullptr) {
    auto value = std::string(analytics_enabled);
    if (value == "true" || value == "1") {
      opts.analytics_enabled = true;
      opts.analytics_rate = 1.0;
    } else if (value == "false" || value == "0" || value == "") {
      opts.analytics_enabled = false;
      opts.analytics_rate = std::nan("");
    } else {
      return ot::make_unexpected("Value for DD_TRACE_ANALYTICS_ENABLED is invalid");
    }
  }

  auto analytics_rate = std::getenv("DD_TRACE_ANALYTICS_SAMPLE_RATE");
  if (analytics_rate != nullptr) {
    try {
      double value = std::stod(analytics_rate);
      if (value >= 0.0 && value <= 1.0) {
        opts.analytics_enabled = true;
        opts.analytics_rate = value;
      } else {
        return ot::make_unexpected("Value for DD_TRACE_ANALYTICS_SAMPLE_RATE is invalid");
      }
    } catch (const std::invalid_argument &ia) {
      return ot::make_unexpected("Value for DD_TRACE_ANALYTICS_SAMPLE_RATE is invalid");
    } catch (const std::out_of_range &oor) {
      return ot::make_unexpected("Value for DD_TRACE_ANALYTICS_SAMPLE_RATE is invalid");
    }
  }
  return opts;
}

}  // namespace opentracing
}  // namespace datadog
