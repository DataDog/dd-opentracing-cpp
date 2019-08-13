#include "span.h"
#include <datadog/tags.h>
#include <opentracing/ext/tags.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include "sample.h"
#include "span_buffer.h"
#include "tracer.h"

namespace tags = datadog::tags;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {
const std::string event_sample_rate_metric = "_dd1.sr.eausr";
}  // namespace

SpanData::SpanData(std::string type, std::string service, ot::string_view resource,
                   std::string name, uint64_t trace_id, uint64_t span_id, uint64_t parent_id,
                   int64_t start, int64_t duration, int32_t error)
    : type(type),
      service(service),
      resource(resource),
      name(name),
      trace_id(trace_id),
      span_id(span_id),
      parent_id(parent_id),
      start(start),
      duration(duration),
      error(error) {}

SpanData::SpanData() {}

uint64_t SpanData::traceId() const { return trace_id; }
uint64_t SpanData::spanId() const { return span_id; }

const std::string SpanData::env() const {
  const auto &env = meta.find(tags::environment);
  if (env == meta.end()) {
    return "";
  }
  return env->second;
}

std::unique_ptr<SpanData> makeSpanData(std::string type, std::string service,
                                       ot::string_view resource, std::string name,
                                       uint64_t trace_id, uint64_t span_id, uint64_t parent_id,
                                       int64_t start) {
  return std::unique_ptr<SpanData>{
      new SpanData(type, service, resource, name, trace_id, span_id, parent_id, start, 0, 0)};
}

std::unique_ptr<SpanData> stubSpanData() { return std::unique_ptr<SpanData>{new SpanData()}; }

Span::Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<SpanBuffer> buffer,
           TimeProvider get_time, std::shared_ptr<SampleProvider> sampler, uint64_t span_id,
           uint64_t trace_id, uint64_t parent_id, SpanContext context, TimePoint start_time,
           std::string span_service, std::string span_type, std::string span_name,
           std::string resource, std::string operation_name_override)
    : tracer_(std::move(tracer)),
      buffer_(std::move(buffer)),
      get_time_(get_time),
      sampler_(sampler),
      context_(std::move(context)),
      start_time_(start_time),
      operation_name_override_(operation_name_override),
      span_(makeSpanData(span_type, span_service, resource, span_name, trace_id, span_id,
                         parent_id,
                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                             start_time_.absolute_time.time_since_epoch())
                             .count())) {
  buffer_->registerSpan(context_);
}

Span::~Span() {
  if (!is_finished_) {
    this->Finish();
  }
}

namespace {
// Matches path segments with numbers (except things that look like versions).
// Similar to, but not the same as,
// https://github.com/datadog/dd-trace-java/blob/master/dd-trace-ot/src/main/java/datadog/opentracing/decorators/URLAsResourceName.java#L14-L16
std::regex &PATH_MIXED_ALPHANUMERICS() {
  // Don't statically initialize a complex object.
  // Thread safe as of C++11, as long as it's not reentrant.
  static std::regex r{
      "(\\/)(?:(?:([^?\\/&]*)(?:\\?[^\\/]+))|(?:(?![vV]\\d{1,2}\\/)[^\\/"
      "\\d\\?]*[\\d-]+[^\\/]*))"};
  return r;
}
}  // namespace

// Imperfectly audits the data in a Span, removing some things that could cause information leaks
// or cardinality issues.
// If you want to add any more steps to this function, we should use a more
// sophisticated architecture. For now, YAGNI.
void audit(SpanData *span) {
  auto http_tag = span->meta.find(ot::ext::http_url);
  if (http_tag != span->meta.end()) {
    http_tag->second = std::regex_replace(http_tag->second, PATH_MIXED_ALPHANUMERICS(), "$1$2?");
  }
}

void Span::FinishWithOptions(
    const ot::FinishSpanOptions & /* finish_span_options */) noexcept try {
  if (is_finished_.exchange(true)) {
    return;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  // Set end time.
  auto end_time = get_time_();
  span_->duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
  // Override operation name if needed.
  if (operation_name_override_ != "") {
    span_->meta[tags::operation_name] = span_->name;
    span_->name = operation_name_override_;
  }
  // Apply special tags.
  // If we add any more cases; then abstract this. For now, KISS.
  auto tag = span_->meta.find(tags::span_type);
  if (tag != span_->meta.end()) {
    span_->type = tag->second;
    span_->meta.erase(tag);
  }
  tag = span_->meta.find(tags::resource_name);
  if (tag != span_->meta.end()) {
    span_->resource = tag->second;
    span_->meta.erase(tag);
  }
  tag = span_->meta.find(tags::service_name);
  if (tag != span_->meta.end()) {
    span_->service = tag->second;
    span_->meta.erase(tag);
  }
  tag = span_->meta.find(::ot::ext::error);
  if (tag != span_->meta.end()) {
    // tag->second is the JSON-serialized value of the variadic type given to SetTag. If it's
    // clearly falsey then set the error flag.
    if (tag->second == "0" || tag->second == "" || tag->second == "false") {
      span_->error = 0;
    } else {
      span_->error = 1;
    }
    // Don't erase the tag, in case it is populated with interesting information.
  }
  tag = span_->meta.find(tags::analytics_event);
  if (tag != span_->meta.end()) {
    // tag->second is the JSON-serialized value of the variadic type given to SetTag.
    // Apply boolean, valid integer and valid double's.
    if (tag->second == "true" || tag->second == "1") {
      span_->metrics[event_sample_rate_metric] = 1.0;
    } else if (tag->second == "false" || tag->second == "0" || tag->second == "") {
      span_->metrics[event_sample_rate_metric] = 0.0;
    } else {
      // Check if the value is a double between 0.0 and 1.0 (inclusive).
      try {
        double value = std::stod(tag->second);
        if (value >= 0.0 && value <= 1.0) {
          span_->metrics[event_sample_rate_metric] = value;
        }
      } catch (const std::invalid_argument &ia) {
        // Ignore invalid value.
      } catch (const std::out_of_range &oor) {
        // Ignore values not in range.
      }
    }

    span_->meta.erase(tag);
  }
  // Audit and finish span.
  audit(span_.get());
  buffer_->finishSpan(std::move(span_), sampler_);
  // According to the OT lifecycle, no more methods should be called on this Span. But just in case
  // let's make sure that span_ isn't nullptr. Fine line between defensive programming and voodoo.
  span_ = stubSpanData();
} catch (const std::bad_alloc &) {
  // At least don't crash.
}

void Span::SetOperationName(ot::string_view operation_name) noexcept {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  span_->name = operation_name;
  span_->resource = operation_name;
}

namespace {
// Visits and serializes an arbitrarily-nested variant type. Serialisation of value types is to
// string while any composite types are expressed in JSON. eg. string("fred") -> "fred"
// vector<string>{"felicity"} -> "[\"felicity\"]"
struct VariantVisitor {
  // Populated with the final result.
  std::string &result;
  VariantVisitor(std::string &result_) : result(result_) {}

 private:
  // Only set if VariantVisitor is recursing. Unfortunately we only really need an explicit
  // distinction (and all the conditionals below) to avoid the case of a simple string being
  // serialized to "\"string\"" - which is valid JSON but very silly never-the-less.
  json *json_result = nullptr;
  VariantVisitor(std::string &result_, json *json_result_)
      : result(result_), json_result(json_result_) {}

 public:
  void operator()(bool value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = value ? "true" : "false";
    }
  }

  void operator()(double value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = std::to_string(value);
    }
  }

  void operator()(int64_t value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = std::to_string(value);
    }
  }

  void operator()(uint64_t value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = std::to_string(value);
    }
  }

  void operator()(const std::string &value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = value;
    }
  }

  void operator()(std::nullptr_t) const {
    if (json_result != nullptr) {
      *json_result = "nullptr";
    } else {
      result = "nullptr";
    }
  }

  void operator()(const char *value) const {
    if (json_result != nullptr) {
      *json_result = value;
    } else {
      result = std::string(value);
    }
  }

  void operator()(const std::vector<ot::Value> &values) const {
    json list;
    for (auto value : values) {
      json inner;
      std::string r;
      apply_visitor(VariantVisitor{r, &inner}, value);
      list.push_back(inner);
    }
    if (json_result != nullptr) {
      // We're a list in a dict/list.
      *json_result = list;
    } else {
      // We're a root object, so dump the string.
      result = list.dump();
    }
  }

  void operator()(const std::unordered_map<std::string, ot::Value> &value) const {
    json dict;
    for (auto pair : value) {
      json inner;
      std::string r;
      apply_visitor(VariantVisitor{r, &inner}, pair.second);
      dict[pair.first] = inner;
    }
    if (json_result != nullptr) {
      // We're a dict in a dict/list.
      *json_result = dict;
    } else {
      // We're a root object, so dump the string.
      result = dict.dump();
    }
  }
};
}  // namespace

// Normalizes the tag key.
// For now:
// - ':' is replaced with '.'
// Further normalization may be done in the future, such as
// converting to lowercase, and replacing spaces and other punctuation
// with underscore.
std::string normalizeTagKey(std::string tag) {
  std::replace(tag.begin(), tag.end(), ':', '.');
  return tag;
}

void Span::SetTag(ot::string_view key, const ot::Value &value) noexcept {
  std::string k = normalizeTagKey(key);
  std::string result;
  apply_visitor(VariantVisitor{result}, value);
  {
    std::lock_guard<std::mutex> lock_guard{mutex_};
    span_->meta[k] = result;
  }

  // Normally special tags are processed at Span Finish, but this cannot be done for
  // sampling tags because if no sampling is set before the Span Finishes then one is
  // assigned immutably.
  // The sampling tags are "sampling.priority", "manual.keep" and "manual.drop".
  // Doesn't need to be in the same mutex lock as above.
  if (k == ::ot::ext::sampling_priority) {
    // https://github.com/opentracing/specification/blob/master/semantic_conventions.md#span-tags-table
    // "sampling.priority"
    try {
      std::unique_ptr<UserSamplingPriority> sampling_priority = nullptr;
      if (result != "") {
        sampling_priority = std::make_unique<UserSamplingPriority>(
            std::stoi(result) == 0 ? UserSamplingPriority::UserDrop
                                   : UserSamplingPriority::UserKeep);
      }
      setSamplingPriority(std::move(sampling_priority));
    } catch (const std::invalid_argument &ia) {
      std::cerr << "Unable to parse " << ::ot::ext::sampling_priority << " tag" << std::endl;
    } catch (const std::out_of_range &oor) {
      std::cerr << "Unable to parse " << ::ot::ext::sampling_priority << " tag" << std::endl;
    }
  } else if (k == tags::manual_keep) {
    setSamplingPriority(std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserKeep));
  } else if (k == tags::manual_drop) {
    setSamplingPriority(std::make_unique<UserSamplingPriority>(UserSamplingPriority::UserDrop));
  }
}

void Span::SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept {
  context_.setBaggageItem(restricted_key, value);
}

std::string Span::BaggageItem(ot::string_view restricted_key) const noexcept {
  return context_.baggageItem(restricted_key);
}

void Span::Log(
    std::initializer_list<std::pair<ot::string_view, ot::Value>> /* fields */) noexcept {}

OptionalSamplingPriority Span::setSamplingPriority(
    std::unique_ptr<UserSamplingPriority> user_priority) {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  OptionalSamplingPriority priority(nullptr);
  if (user_priority != nullptr) {
    priority = asSamplingPriority(static_cast<int>(*user_priority));
  }
  return buffer_->setSamplingPriority(context_.traceId(), std::move(priority));
}

OptionalSamplingPriority Span::getSamplingPriority() const {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  return buffer_->getSamplingPriority(context_.traceId());
}

const ot::SpanContext &Span::context() const noexcept {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  // First apply sampling. This concern sits more reasonably upon the destructor/Finish method - to
  // ensure that users have every chance to apply their own SamplingPriority before one is decided.
  // However, OpenTracing serializes the SpanContext from a Span *before* finishing that Span. So
  // on-Span-finishing is too late to work out whether to sample or not. Therefore, we must do it
  // here, when the context is fetched before being serialized. The negative side-effect is that if
  // anything else happens to want to get and/or serialize a SpanContext, that will end up having
  // this spooky action at a distance of assigning a SamplingPriority.
  buffer_->assignSamplingPriority(sampler_, span_.get() /* Doesn't take ownership */);
  return context_;
}

const ot::Tracer &Span::tracer() const noexcept { return *tracer_; }

uint64_t Span::traceId() const {
  return span_->trace_id;  // Never modified, hence un-locked access.
}

uint64_t Span::spanId() const {
  return span_->span_id;  // Never modified, hence un-locked access.
}

}  // namespace opentracing
}  // namespace datadog
