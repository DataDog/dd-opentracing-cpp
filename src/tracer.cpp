#include <datadog/opentracing.h>
#include <datadog/tags.h>
#include <errno.h>
#include <opentracing/ext/tags.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
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
  TlsRandomNumberGenerator() { pthread_atfork(nullptr, nullptr, onFork); }

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
  /*
  // C++17's filesystem api would be really nice to have right now..
  std::string startup_log_path = "/var/tmp/dd-opentracing-cpp";
  struct stat s;
  if (stat(startup_log_path.c_str(), &s) == 0) {
    if (!S_ISDIR(s.st_mode)) {
      // Path exists but isn't a directory.
      return;
    }
  } else {
    if (errno != ENOENT) {
      // Failed to stat directory, but reason was something other than
      // not existing.
      return;
    }
    if (mkdir(startup_log_path.c_str(), 01777) != 0) {
      // Unable to create the log path.
      return;
    }
  }

  auto now = std::chrono::system_clock::now();
  uint64_t timestamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  std::ofstream log_file(startup_log_path + "/startup_options-" + std::to_string(timestamp) +
                         ".json");
  if (!log_file.good()) {
    return;
  }
  logTracerOptions(now, options, log_file);
  log_file.close();
  */
}

}  // namespace

void Tracer::configureRulesSampler(std::shared_ptr<RulesSampler> sampler) noexcept try {
  auto log_invalid_json = [&](const std::string &description, json &object) {
    logger_->Log(LogLevel::info, description + ": " + object.get<std::string>());
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
  logger_->Log(
      LogLevel::error,
      std::string("rules sampler: unable to parse JSON config for rules sampler: ", error.what()));
}

Tracer::Tracer(TracerOptions options, std::shared_ptr<SpanBuffer> buffer, TimeProvider get_time,
               IdProvider get_id)
    : opts_(options),
      buffer_(std::move(buffer)),
      get_time_(get_time),
      get_id_(get_id),
      legacy_obfuscation_(legacyObfuscationEnabled()) {}

Tracer::Tracer(TracerOptions options, std::shared_ptr<Writer> writer,
               std::shared_ptr<RulesSampler> sampler)
    : opts_(options),
      get_time_(getRealTime),
      get_id_(getId),
      legacy_obfuscation_(legacyObfuscationEnabled()) {
  if (isDebug()) {
    logger_ = std::make_shared<VerboseLogger>(opts_.log_func);
  } else {
    logger_ = std::make_shared<StandardLogger>(opts_.log_func);
  }
  configureRulesSampler(sampler);
  startupLog(options);
  buffer_ = std::make_shared<WritingSpanBuffer>(
      logger_, writer, sampler,
      WritingSpanBufferOptions{isEnabled(), reportingHostname(options), analyticsRate(options)});
}

std::unique_ptr<ot::Span> Tracer::StartSpanWithOptions(ot::string_view operation_name,
                                                       const ot::StartSpanOptions &options) const
    noexcept try {
  // Get a new span id.
  auto span_id = get_id_();

  SpanContext span_context = SpanContext{logger_, span_id, span_id, "", {}};
  // See the comment in propagation.h on nginx_opentracing_compatibility_hack_.
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

}  // namespace opentracing
}  // namespace datadog
