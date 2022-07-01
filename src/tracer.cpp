#include <datadog/opentracing.h>
#include <datadog/tags.h>
#include <errno.h>
#include <opentracing/ext/tags.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <winsock.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <cassert>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

#include "bool.h"
#include "parse_util.h"
#include "tracer.h"

namespace ot = opentracing;
namespace tags = datadog::tags;

namespace datadog {
namespace opentracing {

// Wrapper for a seeded random number generator that works with forking.
//
// See https://stackoverflow.com/q/51882689/4447365 and
//     https://github.com/opentracing-contrib/nginx-opentracing/issues/52
namespace {
class TlsRandomNumberGenerator {
 public:
  TlsRandomNumberGenerator() {
#ifdef _MSC_VER
// When compiling with MSVC, pthreads are not used.
// TODO: investigate equivalent of pthread_atfork for MSVC
#else
    pthread_atfork(nullptr, nullptr, onFork);
#endif
  }

  static std::mt19937_64 &generator() { return random_number_generator_; }

 private:
  static thread_local std::mt19937_64 random_number_generator_;

  static void onFork() { random_number_generator_.seed(std::random_device{}()); }
};
}  // namespace

thread_local std::mt19937_64 TlsRandomNumberGenerator::random_number_generator_{
    std::random_device{}()};

uint64_t getId() {
  static TlsRandomNumberGenerator rng;
  static thread_local std::uniform_int_distribution<int64_t> distribution;
  return distribution(TlsRandomNumberGenerator::generator());
}

namespace {

bool isEnabled() {
  auto enabled = std::getenv("DD_TRACE_ENABLED");
  // defaults to true unless env var is set to "false"
  if (enabled != nullptr && !stob(enabled, true)) {
    return false;
  }
  return true;
}

std::string reportingHostname(TracerOptions options) {
  // This returns the machine name when the tracer has been configured
  // to report hostnames.
  if (options.report_hostname) {
    char buffer[256];
    if (!::gethostname(buffer, 256)) {
      return std::string(buffer);
    }
  }
  return "";
}

double analyticsRate(TracerOptions options) {
  if (options.analytics_rate >= 0.0 && options.analytics_rate <= 1.0) {
    return options.analytics_rate;
  }
  return std::nan("");
}

bool legacyObfuscationEnabled() {
  auto obfuscation = std::getenv("DD_TRACE_CPP_LEGACY_OBFUSCATION");
  if (obfuscation != nullptr && std::string(obfuscation) == "1") {
    return true;
  }
  return false;
}

void startupLog(TracerOptions &options) {
  auto env_setting = std::getenv("DD_TRACE_STARTUP_LOGS");
  if (env_setting != nullptr && !stob(env_setting, true)) {
    // Startup logs are disabled.
    return;
  }

  std::string message;
  message += "DATADOG TRACER CONFIGURATION - ";
  const bool with_timestamp = true;
  message += toJSON(options, with_timestamp);
  options.log_func(LogLevel::info, message);
}

uint64_t traceTagsPropagationMaxLength(const TracerOptions &options, const Logger &logger) {
  const char env_name[] = "DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH";
  const char *const env_value = std::getenv(env_name);
  if (env_value == nullptr) {
    return options.tags_header_size;
  }

  try {
    return parse_uint64(env_value, 10);
  } catch (const std::logic_error &error) {
    std::string message = error.what();
    message += ": Unable to parse integer from ";
    message += env_name;
    message += " environment variable value: ";
    message += env_value;
    logger.Log(LogLevel::error, message);
    return options.tags_header_size;
  }
}

std::string spanSamplingRules(const TracerOptions &options, const Logger &logger) {
  // Prefer DD_SPAN_SAMPLING_RULES, if present.
  // Next, prefer DD_SPAN_SAMPLING_RULES_FILE.
  // If both are specified, log an error and use DD_SPAN_SAMPLING_RULES.
  // If neither are specified, use `options.span_sampling_rules`.

  const char *const span_rules = std::getenv("DD_SPAN_SAMPLING_RULES");
  const char *const span_rules_file = std::getenv("DD_SPAN_SAMPLING_RULES_FILE");
  if (span_rules) {
    if (span_rules_file) {
      logger.Log(LogLevel::error,
                 "Both DD_SPAN_SAMPLING_RULES and DD_SPAN_SAMPLING_RULES_FILE have values in the "
                 "environment.  DD_SPAN_SAMPLING_RULES will be used, and "
                 "DD_SPAN_SAMPLING_RULES_FILE will be ignored.");
    }
    return span_rules;
  }

  if (span_rules_file) {
    const auto log_file_error = [&](const char *operation) {
      std::string message;
      message += "Unable to ";
      message += operation;
      message += " file \"";
      message += span_rules_file;
      message += "\" specified as value of environment variable DD_SPAN_SAMPLING_RULES_FILE.";
      logger.Log(LogLevel::error, message);
    };

    std::ifstream file(span_rules_file);
    if (!file) {
      log_file_error("open");
      return options.span_sampling_rules;
    }

    std::stringstream span_rules;
    span_rules << file.rdbuf();
    if (!file) {
      log_file_error("read");
      return options.span_sampling_rules;
    }

    return span_rules.str();
  }

  return options.span_sampling_rules;
}

}  // namespace

void Tracer::configureRulesSampler(std::shared_ptr<RulesSampler> sampler) noexcept {
  try {
    auto log_invalid_json = [&](const std::string &description, json &object) {
      logger_->Log(LogLevel::error, description + ": " + object.get<std::string>());
    };
    json config = json::parse(opts_.sampling_rules);
    for (auto &item : config.items()) {
      auto rule = item.value();
      if (!rule.is_object()) {
        log_invalid_json("rules sampler: unexpected item in sampling rules", rule);
        continue;
      }
      // "sample_rate" is mandatory
      if (!rule.contains("sample_rate")) {
        log_invalid_json("rules sampler: rule is missing 'sample_rate'", rule);
        continue;
      }
      if (!rule.at("sample_rate").is_number()) {
        log_invalid_json("rules sampler: invalid type for 'sample_rate' (expected number)", rule);
        continue;
      }
      auto sample_rate = rule.at("sample_rate").get<json::number_float_t>();
      if (!(sample_rate >= 0.0 && sample_rate <= 1.0)) {
        log_invalid_json(
            "rules sampler: invalid value for sample rate (expected value between 0.0 and 1.0)",
            rule);
        continue;
      }
      // "service" and "name" are optional
      bool has_service = rule.contains("service") && rule.at("service").is_string();
      bool has_name = rule.contains("name") && rule.at("name").is_string();
      auto nan = std::nan("");
      if (has_service && has_name) {
        auto svc = rule.at("service").get<std::string>();
        auto nm = rule.at("name").get<std::string>();
        sampler->addRule([=](const std::string &service, const std::string &name) -> RuleResult {
          if (service == svc && name == nm) {
            return {true, sample_rate};
          }
          return {false, nan};
        });
      } else if (has_service) {
        auto svc = rule.at("service").get<std::string>();
        sampler->addRule([=](const std::string &service, const std::string &) -> RuleResult {
          if (service == svc) {
            return {true, sample_rate};
          }
          return {false, nan};
        });
      } else if (has_name) {
        auto nm = rule.at("name").get<std::string>();
        sampler->addRule([=](const std::string &, const std::string &name) -> RuleResult {
          if (name == nm) {
            return {true, sample_rate};
          }
          return {false, nan};
        });
      } else {
        sampler->addRule([=](const std::string &, const std::string &) -> RuleResult {
          return {true, sample_rate};
        });
      }
    }
  } catch (const json::parse_error &error) {
    std::ostringstream message;
    message << "rules sampler: unable to parse JSON config for rules sampler: " << error.what();
    logger_->Log(LogLevel::error, message.str());
  }

  // If there is a configured overall sample rate, add an automatic "catch all"
  // rule to the end that samples at that rate.  Otherwise, don't (unmatched
  // traces will be subject to priority sampling).
  const double sample_rate = opts_.sample_rate;
  if (!std::isnan(sample_rate)) {
    sampler->addRule([=](const std::string &, const std::string &) -> RuleResult {
      return {true, sample_rate};
    });
  }
}

Tracer::Tracer(TracerOptions options, std::shared_ptr<SpanBuffer> buffer, TimeProvider get_time,
               IdProvider get_id, std::shared_ptr<const Logger> logger)
    : logger_(logger ? logger : std::make_shared<StandardLogger>(options.log_func)),
      opts_(options),
      buffer_(std::move(buffer)),
      get_time_(get_time),
      get_id_(get_id),
      legacy_obfuscation_(legacyObfuscationEnabled()) {}

Tracer::Tracer(TracerOptions options, std::shared_ptr<Writer> writer,
               std::shared_ptr<RulesSampler> trace_sampler, std::shared_ptr<const Logger> logger)
    : logger_(logger),
      opts_(options),
      get_time_(getRealTime),
      get_id_(getId),
      legacy_obfuscation_(legacyObfuscationEnabled()) {
  assert(logger_);
  configureRulesSampler(trace_sampler);
  auto span_sampler = std::make_shared<SpanSampler>();
  span_sampler->configure(spanSamplingRules(opts_, *logger_), *logger_, get_time_);
  startupLog(options);
  buffer_ = std::make_shared<SpanBuffer>(
      logger_, writer, trace_sampler, span_sampler,
      SpanBufferOptions{isEnabled(), reportingHostname(options), analyticsRate(options),
                        options.service, traceTagsPropagationMaxLength(options, *logger_)});
}

std::unique_ptr<ot::Span> Tracer::StartSpanWithOptions(ot::string_view operation_name,
                                                       const ot::StartSpanOptions &options) const
    noexcept try {
  // Generate a span ID for the new span to use.
  auto span_id = get_id_();

  SpanContext span_context = SpanContext{logger_, span_id, span_id, "", {}};
  // See the comment in span_context.h on nginx_opentracing_compatibility_hack_.
  if (operation_name == "dummySpan") {
    span_context =
        SpanContext::NginxOpenTracingCompatibilityHackSpanContext(logger_, span_id, span_id, {});
  }
  auto trace_id = span_id;
  auto parent_id = uint64_t{0};

  // Create context from parent context if possible.
  for (auto &reference : options.references) {
    if (auto parent_context = dynamic_cast<const SpanContext *>(reference.second)) {
      span_context = parent_context->withId(span_id);
      trace_id = parent_context->traceId();
      parent_id = parent_context->id();
      break;
    }
  }

  auto span = std::make_unique<Span>(logger_, shared_from_this(), buffer_, get_time_, span_id,
                                     trace_id, parent_id, std::move(span_context), get_time_(),
                                     opts_.service, opts_.type, operation_name, operation_name,
                                     opts_.operation_name_override, legacy_obfuscation_);

  if (!opts_.environment.empty()) {
    span->SetTag(datadog::tags::environment, opts_.environment);
  }
  if (!opts_.version.empty()) {
    span->SetTag(datadog::tags::version, opts_.version);
  }

  for (auto &tag : opts_.tags) {
    span->SetTag(tag.first, tag.second);
  }
  for (auto &tag : options.tags) {
    if (tag.first == ::ot::ext::sampling_priority && span->getSamplingPriority() != nullptr) {
      // Do not apply this tag if sampling priority is already assigned.
      continue;
    }
    span->SetTag(tag.first, tag.second);
  }
  return span;
} catch (const std::bad_alloc &) {
  // At least don't crash.
  return nullptr;
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc, std::ostream &writer) const {
  auto span_context = dynamic_cast<const SpanContext *>(&sc);
  if (span_context == nullptr) {
    return ot::make_unexpected(ot::invalid_span_context_error);
  }
  return span_context->serialize(writer, buffer_, opts_.priority_sampling);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::TextMapWriter &writer) const {
  auto span_context = dynamic_cast<const SpanContext *>(&sc);
  if (span_context == nullptr) {
    return ot::make_unexpected(ot::invalid_span_context_error);
  }
  return span_context->serialize(writer, buffer_, opts_.inject, opts_.priority_sampling);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::HTTPHeadersWriter &writer) const {
  auto span_context = dynamic_cast<const SpanContext *>(&sc);
  if (span_context == nullptr) {
    return ot::make_unexpected(ot::invalid_span_context_error);
  }
  return span_context->serialize(writer, buffer_, opts_.inject, opts_.priority_sampling);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(std::istream &reader) const {
  return SpanContext::deserialize(logger_, reader);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::TextMapReader &reader) const {
  return SpanContext::deserialize(logger_, reader, opts_.extract);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::HTTPHeadersReader &reader) const {
  return SpanContext::deserialize(logger_, reader, opts_.extract);
}

void Tracer::Close() noexcept { buffer_->flush(std::chrono::seconds(5)); }

const TracerOptions &Tracer::options() const noexcept { return opts_; }

const TracerOptions &getOptions(const ot::Tracer &tracer) {
  auto &dd_tracer = static_cast<const Tracer &>(tracer);
  return dd_tracer.options();
}

}  // namespace opentracing
}  // namespace datadog
