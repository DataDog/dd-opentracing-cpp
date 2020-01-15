#include <datadog/opentracing.h>
#include <datadog/tags.h>
#include <opentracing/ext/tags.h>
#include <pthread.h>
#include <unistd.h>
#include <random>

#include "noopspan.h"
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

Tracer::Tracer(TracerOptions options, std::shared_ptr<SpanBuffer> buffer, TimeProvider get_time,
               IdProvider get_id, std::shared_ptr<SampleProvider> sampler)
    : opts_(options),
      buffer_(std::move(buffer)),
      get_time_(get_time),
      get_id_(get_id),
      sampler_(sampler) {}

Tracer::Tracer(TracerOptions options, std::shared_ptr<Writer> &writer,
               std::shared_ptr<SampleProvider> sampler)
    : opts_(options), get_time_(getRealTime), get_id_(getId), sampler_(sampler) {
  buffer_ = std::shared_ptr<SpanBuffer>{new WritingSpanBuffer{
      writer, WritingSpanBufferOptions{reportingHostname(options), analyticsRate(options)}}};
}

std::unique_ptr<ot::Span> Tracer::StartSpanWithOptions(ot::string_view operation_name,
                                                       const ot::StartSpanOptions &options) const
    noexcept try {
  // Get a new span id.
  auto span_id = get_id_();

  SpanContext span_context = SpanContext{span_id, span_id, "", {}};
  // See the comment in propagation.h on nginx_opentracing_compatibility_hack_.
  if (operation_name == "dummySpan") {
    span_context = SpanContext::NginxOpenTracingCompatibilityHackSpanContext(span_id, span_id, {});
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

  // Check early if we need to discard. Check at span Finish if we need to sample (since users can
  // set this).
  if (span_context.getPropagatedSamplingPriority() == nullptr && sampler_->discard(span_context)) {
    return std::unique_ptr<ot::Span>{
        new NoopSpan{shared_from_this(), span_id, trace_id, parent_id, std::move(span_context)}};
  }
  std::unique_ptr<Span> span{new Span{shared_from_this(), buffer_, get_time_, sampler_, span_id,
                                      trace_id, parent_id, std::move(span_context), get_time_(),
                                      opts_.service, opts_.type, operation_name, operation_name,
                                      opts_.operation_name_override}};
  bool is_trace_root = parent_id == 0;
  for (auto &tag : options.tags) {
    if (tag.first == ::ot::ext::sampling_priority && span->getSamplingPriority() != nullptr) {
      // Do not apply this tag if sampling priority is already assigned.
      continue;
    }
    span->SetTag(tag.first, tag.second);
  }
  if (is_trace_root && opts_.environment != "") {
    span->SetTag(tags::environment, opts_.environment);
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
  return SpanContext::deserialize(reader);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::TextMapReader &reader) const {
  return SpanContext::deserialize(reader, opts_.extract);
}

ot::expected<std::unique_ptr<ot::SpanContext>> Tracer::Extract(
    const ot::HTTPHeadersReader &reader) const {
  return SpanContext::deserialize(reader, opts_.extract);
}

void Tracer::Close() noexcept { buffer_->flush(std::chrono::seconds(5)); }

}  // namespace opentracing
}  // namespace datadog
