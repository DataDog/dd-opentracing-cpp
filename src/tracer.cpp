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

#include <fstream>
#include <random>

#include "bool.h"
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

bool isDebug() {
  auto debug = std::getenv("DD_TRACE_DEBUG");
  // defaults to false unless env var is set to "true"
  if (debug != nullptr && stob(debug, false)) {
    return true;
  }
  return false;
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

  json j;
  std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::stringstream ss;
  ss << std::put_time(std::localtime(&t), "%FT%T%z");
  j["date"] = ss.str();
  j["version"] = datadog::version::tracer_version;
  j["lang"] = "cpp";
  j["lang_version"] = datadog::version::cpp_version;
  j["env"] = options.environment;
  j["enabled"] = true;
  j["service"] = options.service;
  if (!options.agent_url.empty()) {
    j["agent_url"] = options.agent_url;
  } else {
    j["agent_url"] =
        std::string("http://") + options.agent_host + ":" + std::to_string(options.agent_port);
  }
  j["analytics_enabled"] = options.analytics_enabled;
  j["analytics_sample_rate"] = options.analytics_rate;
  j["sampling_rules"] = options.sampling_rules;
  if (!options.tags.empty()) {
    j["tags"] = options.tags;
  }
  if (!options.version.empty()) {
    j["dd_version"] = options.version;
  }
  j["report_hostname"] = options.report_hostname;
  if (!options.operation_name_override.empty()) {
    j["operation_name_override"] = options.operation_name_override;
  }

  std::string message;
  message += "DATADOG TRACER CONFIGURATION - ";
  message += j.dump();
  options.log_func(LogLevel::info, message);
}

void configureRulesSampler(std::shared_ptr<const Logger> logger,
                           std::shared_ptr<RulesSampler> sampler,
                           std::string sampling_rules) noexcept try {
  auto log_invalid_json = [&](const std::string &description, json &object) {
    logger->Log(LogLevel::info, description + ": " + object.get<std::string>());
  };
  json config = json::parse(sampling_rules);
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
  logger->Log(
      LogLevel::error,
      std::string("rules sampler: unable to parse JSON config for rules sampler: ", error.what()));
}

struct CreateContextResult {
  SpanContext context;
  uint64_t parent_id;
  bool new_trace;
};

// Checks for special NGINX context, or a parent context from the span options,
// or falls back to creating a new context.
CreateContextResult createContext(std::shared_ptr<const Logger> logger,
                                  std::shared_ptr<RulesSampler> sampler,
                                  std::shared_ptr<Writer> writer, ot::string_view operation_name,
                                  const ot::StartSpanOptions &options, uint64_t span_id) {
  if (operation_name == "dummySpan") {
    return {SpanContext::NginxOpenTracingCompatibilityHackSpanContext(logger, sampler, span_id,
                                                                      span_id),
            0, true};
  }
  // Create context from parent context if possible.
  for (auto &reference : options.references) {
    if (auto parent_context = dynamic_cast<const SpanContext *>(reference.second)) {
      return {parent_context->childContext(span_id), parent_context->id(),
              parent_context->extracted()};
    }
  }

  // Create a new span context.
  return {SpanContext(logger, sampler, std::make_shared<ActiveTrace>(logger, writer, span_id),
                      span_id, span_id, "", {}),
          0, true};
}

}  // namespace

Tracer::Tracer(TracerOptions options, TimeProvider get_time, IdProvider get_id)
    : opts_(options),
      get_time_(get_time),
      get_id_(get_id),
      legacy_obfuscation_(legacyObfuscationEnabled()) {
  (void)analyticsRate;
  (void)reportingHostname;
  (void)isEnabled;
}

Tracer::Tracer(TracerOptions options, std::shared_ptr<Writer> writer,
               std::shared_ptr<RulesSampler> sampler)
    : writer_(writer),
      sampler_(sampler),
      opts_(options),
      get_time_(getRealTime),
      get_id_(getId),
      legacy_obfuscation_(legacyObfuscationEnabled()) {
  if (isDebug()) {
    logger_ = std::make_shared<VerboseLogger>(opts_.log_func);
  } else {
    logger_ = std::make_shared<StandardLogger>(opts_.log_func);
  }
  configureRulesSampler(logger_, sampler_, opts_.sampling_rules);
  startupLog(options);
}

std::unique_ptr<ot::Span> Tracer::StartSpanWithOptions(ot::string_view operation_name,
                                                       const ot::StartSpanOptions &options) const
    noexcept try {
  // Get a new span id.
  auto span_id = get_id_();
  auto result = createContext(logger_, sampler_, writer_, operation_name, options, span_id);
  auto context = result.context;

  auto span = std::make_unique<Span>(
      logger_, shared_from_this(), context.activeTrace(), get_time_, span_id, context.traceId(),
      result.parent_id, context, get_time_(), opts_.service, opts_.type, operation_name,
      operation_name, opts_.operation_name_override, legacy_obfuscation_);

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
    if (tag.first == ::ot::ext::sampling_priority && span->samplingStatus().is_set) {
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

  auto active_trace = span_context->activeTrace();
  if (!active_trace->samplingStatus().is_set) {
    // We need to update the sampling status, so temporarily make the span context mutable.
    auto mutable_span_context = const_cast<SpanContext *>(span_context);
    if (mutable_span_context == nullptr) {
      return ot::make_unexpected(ot::span_context_corrupted_error);
    }
    mutable_span_context->sample();
  }

  return span_context->serialize(writer);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::TextMapWriter &writer) const {
  auto span_context = dynamic_cast<const SpanContext *>(&sc);
  if (span_context == nullptr) {
    return ot::make_unexpected(ot::invalid_span_context_error);
  }

  auto active_trace = span_context->activeTrace();
  if (!active_trace->samplingStatus().is_set) {
    // We need to update the sampling status, so temporarily make the span context mutable.
    auto mutable_span_context = const_cast<SpanContext *>(span_context);
    if (mutable_span_context == nullptr) {
      return ot::make_unexpected(ot::span_context_corrupted_error);
    }
    mutable_span_context->sample();
  }

  return span_context->serialize(writer, opts_.inject);
}

ot::expected<void> Tracer::Inject(const ot::SpanContext &sc,
                                  const ot::HTTPHeadersWriter &writer) const {
  auto span_context = dynamic_cast<const SpanContext *>(&sc);
  if (span_context == nullptr) {
    return ot::make_unexpected(ot::invalid_span_context_error);
  }

  auto active_trace = span_context->activeTrace();
  if (!active_trace->samplingStatus().is_set) {
    // We need to update the sampling status, so temporarily make the span context mutable.
    auto mutable_span_context = const_cast<SpanContext *>(span_context);
    if (mutable_span_context == nullptr) {
      return ot::make_unexpected(ot::span_context_corrupted_error);
    }
    mutable_span_context->sample();
  }

  return span_context->serialize(writer, opts_.inject);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(std::istream &reader) const {
  return SpanContext::deserialize(logger_, sampler_, writer_, reader);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::TextMapReader &reader) const {
  return SpanContext::deserialize(logger_, sampler_, writer_, reader, opts_.extract);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::HTTPHeadersReader &reader) const {
  return SpanContext::deserialize(logger_, sampler_, writer_, reader, opts_.extract);
}

void Tracer::Close() noexcept { writer_->flush(std::chrono::seconds(5)); }

}  // namespace opentracing
}  // namespace datadog
