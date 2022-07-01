#include "sample.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include "clock.h"
#include "glob.h"
#include "logger.h"
#include "span.h"

namespace datadog {
namespace opentracing {

namespace {
// Constants used for the Knuth hashing, same constants as the Agent.
constexpr double max_trace_id_double = static_cast<double>(std::numeric_limits<uint64_t>::max());
constexpr uint64_t constant_rate_hash_factor = UINT64_C(1111111111111111111);

const std::string priority_sampler_default_rate_key = "service:,env:";

uint64_t maxIdFromSampleRate(double rate) {
  // This check is required to avoid undefined behaviour converting the rate back from
  // double to uint64_t.
  if (rate == 1.0) {
    return std::numeric_limits<uint64_t>::max();
  } else if (rate > 0.0) {
    return uint64_t(rate * max_trace_id_double);
  }
  return 0;
}
}  // namespace

SampleResult PrioritySampler::sample(const std::string& environment, const std::string& service,
                                     uint64_t trace_id) const {
  SampleResult result;
  SamplingRate applied_rate = default_sample_rate_;
  result.sampling_mechanism = SamplingMechanism::Default;
  std::ostringstream key;
  key << "service:" << service << ",env:" << environment;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto const rule = agent_sampling_rates_.find(key.str());
    if (rule != agent_sampling_rates_.end()) {
      applied_rate = rule->second;
      result.sampling_mechanism = SamplingMechanism::AgentRate;
    }
  }
  // I don't know how voodoo it is to use the trace_id essentially as a source of randomness,
  // rather than generating a new random number here. It's a bit faster, and more importantly it's
  // cargo-culted from the agent. However it does still seem too "clever", and makes testing a
  // bit awkward.
  uint64_t hashed_id = trace_id * constant_rate_hash_factor;
  result.priority_rate = applied_rate.rate;
  if (hashed_id >= applied_rate.max_hash) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerDrop);
  } else {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::SamplerKeep);
  }

  result.applied_rate = applied_rate.rate;
  return result;
}

void PrioritySampler::configure(json config) {
  std::lock_guard<std::mutex> lock{mutex_};
  agent_sampling_rates_.clear();
  for (json::iterator it = config.begin(); it != config.end(); ++it) {
    auto key = it.key();
    auto rate = it.value();
    auto max_hashed = maxIdFromSampleRate(rate);
    if (key == priority_sampler_default_rate_key) {
      default_sample_rate_ = {rate, max_hashed};
    } else {
      agent_sampling_rates_[key] = {rate, max_hashed};
    }
  }
}

RulesSampler::RulesSampler() : sampling_limiter_(getRealTime, 100, 100.0, 1) {}

RulesSampler::RulesSampler(double limit_per_second)
    : sampling_limiter_(getRealTime, limit_per_second) {}

RulesSampler::RulesSampler(TimeProvider clock, long max_tokens, double refresh_rate,
                           long tokens_per_refresh)
    : sampling_limiter_(clock, max_tokens, refresh_rate, tokens_per_refresh) {}

void RulesSampler::addRule(RuleFunc f) { sampling_rules_.push_back(f); }

SampleResult RulesSampler::sample(const std::string& environment, const std::string& service,
                                  const std::string& name, uint64_t trace_id) {
  auto rule_result = match(service, name);
  if (!rule_result.matched) {
    return priority_sampler_.sample(environment, service, trace_id);
  }

  // A sampling rule applies to (matches) the current span.
  //
  // Whatever sampling decision we make here (keep or drop) will be of "user"
  // type, i.e. `SamplingPriority::UserKeep` or `SamplingPriority::UserDrop`.
  //
  // The matching rule's rate was configured by a user, and so we want to make
  // sure that after sending the span to the agent, that the agent does not
  // override our sampling decision as it might for "automated" sampling
  // decisions, i.e. `SamplingPriority::SamplerKeep` or
  // `SamplingPriority::SamplerDrop`.

  SampleResult result;
  result.applied_rate = result.rule_rate = rule_result.rate;
  result.sampling_mechanism = SamplingMechanism::Rule;
  auto max_hash = maxIdFromSampleRate(rule_result.rate);
  uint64_t hashed_id = trace_id * constant_rate_hash_factor;
  if (hashed_id >= max_hash) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);
    return result;
  }

  // Even though the matching sampling rule, above, did not drop this span, we
  // still might drop the span in order to satify the configured maximum
  // sampling rate for spans selected by rule based sampling overall.
  auto limit_result = sampling_limiter_.allow();
  result.applied_rate = result.limiter_rate = limit_result.effective_rate;
  if (limit_result.allowed) {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserKeep);
  } else {
    result.sampling_priority = std::make_unique<SamplingPriority>(SamplingPriority::UserDrop);
  }
  return result;
}

RuleResult RulesSampler::match(const std::string& service, const std::string& name) const {
  static auto nan = std::nan("");
  for (auto& rule : sampling_rules_) {
    auto result = rule(service, name);
    if (result.matched) {
      return result;
    }
  }
  return {false, nan};
}

void RulesSampler::updatePrioritySampler(json config) { priority_sampler_.configure(config); }

SpanSampler::Rule::Config::Config()
    : service_pattern("*"),
      operation_name_pattern("*"),
      sample_rate(1.0),
      max_per_second(std::nan("")),
      text() {}

SpanSampler::Rule::Rule(const SpanSampler::Rule::Config& config, TimeProvider clock)
    : config_(config) {
  if (!std::isnan(config.max_per_second)) {
    limiter_ = std::make_unique<Limiter>(clock, config.max_per_second);
  }
}

bool SpanSampler::Rule::match(const SpanData& span) const {
  const auto is_match = [](const std::string& pattern, const std::string& subject) {
    // Since "*" is the default pattern, optimize for that case.
    return pattern == "*" || glob_match(pattern, subject);
  };

  return is_match(config_.service_pattern, span.service) &&
         is_match(config_.operation_name_pattern, span.name);
}

bool SpanSampler::Rule::sample(const SpanData& span) { return roll(span) && allow(); }

bool SpanSampler::Rule::roll(const SpanData& span) const {
  const uint64_t max_hash = maxIdFromSampleRate(config_.sample_rate);
  // Use the span ID (not the trace ID), so that rolls can differ among spans
  // within the same trace (given the same sample rate).
  const uint64_t hashed_id = span.span_id * constant_rate_hash_factor;
  return hashed_id < max_hash;
}

bool SpanSampler::Rule::allow() {
  if (!limiter_) {
    return true;
  }

  return limiter_->allow().allowed;
}

const SpanSampler::Rule::Config& SpanSampler::Rule::config() const { return config_; }

void SpanSampler::configure(ot::string_view raw_json, const Logger& logger, TimeProvider clock) {
  rules_.clear();

  // `raw_json` is expected to be a JSON array of objects, where each object
  // configures a `SpanSampler::Rule`.
  try {
    const auto log_invalid_json = [&](const std::string& description, json& object) {
      logger.Log(LogLevel::error, description + ": " + object.dump());
    };

    const json config_json = json::parse(raw_json);

    for (auto& item : config_json.items()) {
      auto rule_json = item.value();

      if (!rule_json.is_object()) {
        log_invalid_json("span sampler: unexpected element type in rules array", rule_json);
        continue;
      }

      // Default values are enforced by the `Rule::Config` constructor.
      Rule::Config config;

      if (rule_json.contains("service")) {
        if (!rule_json.at("service").is_string()) {
          log_invalid_json("span sampler: invalid type for 'service' (expected string)",
                           rule_json);
          continue;
        }
        config.service_pattern = rule_json.at("service").get<std::string>();
      }

      if (rule_json.contains("name")) {
        if (!rule_json.at("name").is_string()) {
          log_invalid_json("span sampler: invalid type for 'name' (expected string)", rule_json);
          continue;
        }
        config.operation_name_pattern = rule_json.at("name").get<std::string>();
      }

      if (rule_json.contains("sample_rate")) {
        if (!rule_json.at("sample_rate").is_number()) {
          log_invalid_json("span sampler: invalid type for 'sample_rate' (expected number)",
                           rule_json);
          continue;
        }
        const double sample_rate = rule_json.at("sample_rate").get<json::number_float_t>();
        if (!(sample_rate >= 0.0 && sample_rate <= 1.0)) {
          log_invalid_json(
              "span sampler: invalid value for 'sample_rate' (expected value between 0.0 and 1.0)",
              rule_json);
          continue;
        }
        config.sample_rate = sample_rate;
      }

      if (rule_json.contains("max_per_second")) {
        if (!rule_json.at("max_per_second").is_number()) {
          log_invalid_json("span sampler: invalid type for 'max_per_second' (expected number)",
                           rule_json);
          continue;
        }
        const double max_per_second = rule_json.at("max_per_second").get<json::number_float_t>();
        if (max_per_second <= 0) {
          log_invalid_json(
              "span sampler: invalid value for 'max_per_second' (expected positive value)",
              rule_json);
          continue;
        }
        config.max_per_second = max_per_second;
      }

      config.text = rule_json.dump();
      rules_.emplace_back(config, clock);
    }
  } catch (const json::parse_error& error) {
    std::string message;
    message += "span sampler: unable to parse JSON config: ";
    message += error.what();
    logger.Log(LogLevel::error, message);
  }
}

SpanSampler::Rule* SpanSampler::match(const SpanData& span) {
  const auto found = std::find_if(rules_.begin(), rules_.end(),
                                  [&](const Rule& rule) { return rule.match(span); });
  if (found == rules_.end()) {
    return nullptr;
  }
  return &*found;
}

const std::vector<SpanSampler::Rule>& SpanSampler::rules() const { return rules_; }

}  // namespace opentracing
}  // namespace datadog
