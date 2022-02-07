#include "tracer_options.h"

#include <datadog/tags.h>
#include <opentracing/ext/tags.h>

#include <limits>
#include <regex>
#include <sstream>

#include "bool.h"

namespace ot = opentracing;

namespace datadog {
namespace opentracing {

namespace {
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

// Expands a string into tokens that are separated by commas or whitespace.
// Intended for expanding the propagation style environment variables.
std::vector<std::string> tokenize_propagation_style(const std::string &input) {
  const std::regex word_separator("[\\s,]+");
  std::vector<std::string> result;
  std::copy_if(std::sregex_token_iterator(input.begin(), input.end(), word_separator, -1),
               std::sregex_token_iterator(), std::back_inserter(result),
               [](const std::string &s) { return !s.empty(); });
  return result;
}

ot::expected<double, std::string> parseDouble(const std::string &text, double minimum,
                                              double maximum,
                                              const char *name_for_diagnostic) try {
  std::size_t end_index;
  const double value = std::stod(text, &end_index);
  // If any of the remaining characters are not whitespace, then `text`
  // contains something other than a floating point number.
  if (std::any_of(text.begin() + end_index, text.end(),
                  [](unsigned char ch) { return !std::isspace(ch); })) {
    std::ostringstream error;
    error << name_for_diagnostic << " has trailing non-floating-point characters: " << text;
    return ot::make_unexpected(error.str());
  }
  if (value < minimum || value > maximum) {
    std::ostringstream error;
    error << name_for_diagnostic << " is not within the expected bounds [" << minimum << ", "
          << maximum << "]: " << value;
    return ot::make_unexpected(error.str());
  }
  return value;
} catch (const std::invalid_argument &) {
  std::ostringstream error;
  error << name_for_diagnostic << " does not look like a double: " << text;
  return ot::make_unexpected(error.str());
} catch (const std::out_of_range &) {
  std::ostringstream error;
  error << name_for_diagnostic << " not within the range of a double: " << text;
  return ot::make_unexpected(error.str());
}

}  // namespace

ot::expected<std::set<PropagationStyle>> asPropagationStyle(
    const std::vector<std::string> &styles) {
  std::set<PropagationStyle> propagation_styles;
  for (const std::string &style : styles) {
    if (style == "Datadog") {
      propagation_styles.insert(PropagationStyle::Datadog);
    } else if (style == "B3") {
      propagation_styles.insert(PropagationStyle::B3);
    } else {
      return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  if (propagation_styles.size() == 0) {
    return ot::make_unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  return propagation_styles;
}

ot::expected<TracerOptions, std::string> applyTracerOptionsFromEnvironment(
    const TracerOptions &input) {
  using namespace std::string_literals;

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
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is invalid"s);
    } catch (const std::out_of_range &oor) {
      return ot::make_unexpected("Value for DD_TRACE_AGENT_PORT is out of range"s);
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
    auto style_maybe = asPropagationStyle(tokenize_propagation_style(extract));
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_EXTRACT is invalid"s);
    }
    opts.extract = style_maybe.value();
  }

  auto inject = std::getenv("DD_PROPAGATION_STYLE_INJECT");
  if (inject != nullptr && std::strlen(inject) > 0) {
    auto style_maybe = asPropagationStyle(tokenize_propagation_style(inject));
    if (!style_maybe) {
      return ot::make_unexpected("Value for DD_PROPAGATION_STYLE_INJECT is invalid"s);
    }
    opts.inject = style_maybe.value();
  }

  auto report_hostname = std::getenv("DD_TRACE_REPORT_HOSTNAME");
  if (report_hostname != nullptr) {
    auto value = std::string(report_hostname);
    if (value.empty() || isbool(value)) {
      opts.report_hostname = stob(value, false);
    } else {
      return ot::make_unexpected("Value for DD_TRACE_REPORT_HOSTNAME is invalid"s);
    }
  }

  auto analytics_enabled = std::getenv("DD_TRACE_ANALYTICS_ENABLED");
  if (analytics_enabled != nullptr) {
    auto value = std::string(analytics_enabled);
    if (value.empty() || isbool(value)) {
      opts.analytics_enabled = stob(value, false);
      if (opts.analytics_enabled) {
        opts.analytics_rate = 1.0;
      } else {
        opts.analytics_rate = std::nan("");
      }
    } else {
      return ot::make_unexpected("Value for DD_TRACE_ANALYTICS_ENABLED is invalid"s);
    }
  }

  auto analytics_rate = std::getenv("DD_TRACE_ANALYTICS_SAMPLE_RATE");
  if (analytics_rate != nullptr) {
    auto maybe_value = parseDouble(analytics_rate, 0.0, 1.0, "DD_TRACE_ANALYTICS_SAMPLE_RATE");
    if (!maybe_value) {
      return ot::make_unexpected(maybe_value.error());
    }
    opts.analytics_enabled = true;
    opts.analytics_rate = maybe_value.value();
  }

  auto sampling_limit_per_second = std::getenv("DD_TRACE_RATE_LIMIT");
  if (sampling_limit_per_second != nullptr) {
    auto maybe_value = parseDouble(sampling_limit_per_second, 0.0,
                                   std::numeric_limits<double>::infinity(), "DD_TRACE_RATE_LIMIT");
    if (!maybe_value) {
      return ot::make_unexpected(maybe_value.error());
    }
    opts.sampling_limit_per_second = maybe_value.value();
  }

  return opts;
}

}  // namespace opentracing
}  // namespace datadog
