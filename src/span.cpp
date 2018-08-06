#include "span.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {
const std::string datadog_span_type_tag = "span.type";
const std::string datadog_resource_name_tag = "resource.name";
const std::string datadog_service_name_tag = "service.name";
const std::string http_url_tag = "http.url";
const std::string operation_name_tag = "operation";
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

std::unique_ptr<SpanData> makeSpanData(std::string type, std::string service,
                                       ot::string_view resource, std::string name,
                                       uint64_t trace_id, int64_t span_id, uint64_t parent_id,
                                       int64_t start) {
  return std::unique_ptr<SpanData>{
      new SpanData(type, service, resource, name, trace_id, span_id, parent_id, start, 0, 0)};
}

std::unique_ptr<SpanData> stubSpanData() { return std::unique_ptr<SpanData>{new SpanData()}; }

Span::Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<SpanBuffer> buffer,
           TimeProvider get_time, uint64_t span_id, uint64_t trace_id, uint64_t parent_id,
           SpanContext context, TimePoint start_time, std::string span_service,
           std::string span_type, std::string span_name, std::string resource,
           std::string operation_name_override)
    : tracer_(std::move(tracer)),
      buffer_(std::move(buffer)),
      get_time_(get_time),
      context_(std::move(context)),
      start_time_(start_time),
      operation_name_override_(operation_name_override),
      span_(makeSpanData(span_type, span_service, resource, span_name, trace_id, span_id,
                         parent_id,
                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                             start_time_.absolute_time.time_since_epoch())
                             .count())) {
  buffer_->registerSpan(*span_.get());  // Doens't keep reference.
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
std::regex PATH_MIXED_ALPHANUMERICS{
    "(\\/)(?:(?:([[:alpha:]]*)(?:\\?[^\\/]*))|(?:(?![vV]\\d{1,2}\\/)[^\\/\\d\\?]*[\\d]+[^\\/]*))"};
}  // namespace

// Imperfectly audits the data in a Span, removing some things that could cause information leaks
// or cardinality issues.
// If you want to add any more steps to this function, we should use a more
// sophisticated architecture. For now, YAGNI.
void audit(SpanData *span) {
  auto http_tag = span->meta.find(http_url_tag);
  if (http_tag != span->meta.end()) {
    http_tag->second = std::regex_replace(http_tag->second, PATH_MIXED_ALPHANUMERICS, "$1$2?");
  }
}

void Span::FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept try {
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
    span_->meta[operation_name_tag] = span_->name;
    span_->name = operation_name_override_;
    span_->resource = operation_name_override_;
  }
  // Audit and finish span.
  audit(span_.get());
  buffer_->finishSpan(std::move(span_));
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

void Span::SetTag(ot::string_view key, const ot::Value &value) noexcept {
  std::string result;
  apply_visitor(VariantVisitor{result}, value);
  {
    std::lock_guard<std::mutex> lock_guard{mutex_};
    if (key == datadog_span_type_tag) {
      span_->type = result;
    } else if (key == datadog_resource_name_tag) {
      span_->resource = result;
    } else if (key == datadog_service_name_tag) {
      span_->service = result;
    } else {
      span_->meta[key] = result;
    }
  }
}

void Span::SetBaggageItem(ot::string_view restricted_key, ot::string_view value) noexcept {
  context_.setBaggageItem(restricted_key, value);
}

std::string Span::BaggageItem(ot::string_view restricted_key) const noexcept {
  return context_.baggageItem(restricted_key);
}

void Span::Log(std::initializer_list<std::pair<ot::string_view, ot::Value>> fields) noexcept {}

const ot::SpanContext &Span::context() const noexcept { return context_; }

const ot::Tracer &Span::tracer() const noexcept { return *tracer_; }

uint64_t Span::traceId() const {
  return span_->trace_id;  // Never modified, hence un-locked access.
}

uint64_t Span::spanId() const {
  return span_->span_id;  // Never modified, hence un-locked access.
}

}  // namespace opentracing
}  // namespace datadog
