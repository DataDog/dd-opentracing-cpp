#ifndef DD_OPENTRACING_SAMPLE_H
#define DD_OPENTRACING_SAMPLE_H

#ifdef _MSC_VER
// When compiling with MSVC, std::numeric_limits::max is confused the the max macro,
// and causes compilation errors.
#undef max
#endif

#include <datadog/opentracing.h>
#include <opentracing/tracer.h>

#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

#include "limiter.h"
#include "sampling_mechanism.h"
#include "sampling_priority.h"
#include "span_context.h"

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

struct SampleResult {
  double rule_rate = std::nan("");
  double limiter_rate = std::nan("");
  double priority_rate = std::nan("");
  // `applied_rate` is whichever of `rule_rate`, `limiter_rate`, or
  // `priority_rate` was relevant to this sampling decision, as indicated by
  // `sampling_mechanism`.
  double applied_rate = std::nan("");
  OptionalSamplingPriority sampling_priority = nullptr;
  OptionalSamplingMechanism sampling_mechanism;
};

struct SamplingRate {
  double rate = std::nan("");
  uint64_t max_hash = 0;
};

class PrioritySampler {
 public:
  PrioritySampler() : default_sample_rate_{1.0, std::numeric_limits<uint64_t>::max()} {}
  virtual ~PrioritySampler() {}

  virtual SampleResult sample(const std::string& environment, const std::string& service,
                              uint64_t trace_id) const;
  virtual void configure(json config);

 private:
  mutable std::mutex mutex_;
  std::map<std::string, SamplingRate> agent_sampling_rates_;
  SamplingRate default_sample_rate_;
};

struct RuleResult {
  bool matched = false;
  double rate = std::nan("");
};

using RuleFunc = std::function<RuleResult(const std::string&, const std::string&)>;

class RulesSampler {
 public:
  RulesSampler();
  explicit RulesSampler(double limit_per_second);
  RulesSampler(TimeProvider clock, long max_tokens, double refresh_rate, long tokens_per_refresh);
  // Some of the member functions of this class are declared `virtual` so that
  // they can be overridden by `MockRulesSampler` for use in unit tests.
  virtual ~RulesSampler() {}
  void addRule(RuleFunc f);
  virtual SampleResult sample(const std::string& environment, const std::string& service,
                              const std::string& name, uint64_t trace_id);
  virtual RuleResult match(const std::string& service, const std::string& name) const;
  virtual void updatePrioritySampler(json config);

 private:
  Limiter sampling_limiter_;
  std::vector<RuleFunc> sampling_rules_;
  PrioritySampler priority_sampler_;
};

class Logger;
struct SpanData;

// `SpanSampler` is consulted for each span, but only after another sampler has
// decided that the _trace_ will be dropped (i.e. sampling priority <= 0).
// Span sampling might select individual spans to be kept anyway, based on
// separately configured rules (`DD_SPAN_SAMPLING_RULES`).
//
// Configure `SpanSampling` by calling the `configure` member function.  Then,
// see if a span matches one of the configured rules by calling the `match`
// member function.  If the span matched a rule, then a pointer the rule is
// returned.  The rule's `sample` member function then determines whether the
// span is kept on account of the rule.
class SpanSampler {
 public:
  // `Rule` contains the configuration parsed from a span sampling rule, as
  // well as the associated rate limiter if so configured.
  class Rule {
   public:
    // `Config` contains the parsed configuration for a span sampling rule, as
    // well as the original text of the JSON configuration particular to the
    // rule.
    struct Config {
      std::string service_pattern;         // glob pattern
      std::string operation_name_pattern;  // glob pattern
      double sample_rate;                  // never NaN
      double max_per_second;               // NaN if there is no max
      std::string text;                    // as the rule appeared in the JSON array

      Config();
    };

   private:
    Config config_;
    std::unique_ptr<Limiter> limiter_;

   public:
    // Create a rule having the specified 'config'.  If `config` contains a
    // non-NaN `max_per_second`, then configure the rule's limiter accordingly
    // with the specified `clock`.
    explicit Rule(const Config& config, TimeProvider clock);

    // Return whether the specified `span` matches this rule.
    bool match(const SpanData& span) const;

    // Without checking whether the specified `span` matches this rule, return
    // whether `span` is kept by the rule.
    bool sample(const SpanData& span);

    // Return this rule's configuration.
    const Config& config() const;

   private:
    // Without checking whether the specified `span` matches this rule, and
    // without consulting the limiter, return whether `span` is kept on account
    // of this rule.
    bool roll(const SpanData& span) const;

    // Return whether another span is permitted past this rule's rate limiter.
    // If there is no rate limiter associated with this rule, then this
    // function always returns `true`.
    bool allow();
  };

 private:
  std::vector<Rule> rules_;

 public:
  // Overwrite this sampler's rules with those parsed from the specified
  // `raw_json` configuration text.  Use the specified `clock` for rate
  // limiting.  If an error occurs, skip the offending rule and emit a
  // diagnostic using the specified `logger`.
  void configure(ot::string_view raw_json, const Logger& logger, TimeProvider clock);

  // Return a pointer to the first rule that the specified `span` matches, or
  // return `nullptr` if `span` does not match any configured rule.
  Rule* match(const SpanData& span);

  // Return this sampler's rules.
  const std::vector<Rule>& rules() const;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SAMPLE_H
