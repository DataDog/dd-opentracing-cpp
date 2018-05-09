#include "span.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace ot = opentracing;
using json = nlohmann::json;

namespace datadog {
namespace opentracing {

namespace {
const std::string datadog_span_type_tag = "span.type";
const std::string datadog_resource_name_tag = "resource.name";
const std::string datadog_service_name_tag = "service.name";
const std::string opentracing_component_tag = "component";
}  // namespace

Span::Span(std::shared_ptr<const Tracer> tracer, std::shared_ptr<Writer<Span>> writer,
           TimeProvider get_time, IdProvider next_id, std::string span_service,
           std::string span_type, std::string span_name, ot::string_view resource,
           const ot::StartSpanOptions &options)
    : tracer_(std::move(tracer)),
      get_time_(get_time),
      writer_(std::move(writer)),
      start_time_(get_time_()),
      name(span_name),
      resource(resource),
      service(span_service),
      type(span_type),
      span_id(next_id()),
      trace_id(span_id),
      parent_id(0),
      error(0),
      start(std::chrono::duration_cast<std::chrono::nanoseconds>(
                start_time_.absolute_time.time_since_epoch())
                .count()),
      duration(0),
      context_(span_id, span_id, {}) {
  // Extract context (if present) from options.
  // TODO[willgittoes-dd]: Consider making all this logic happen in the initializer list, so we can
  // make the ID members const.
  const SpanContext *parent_span_context = nullptr;
  for (auto &reference : options.references) {
    if (auto span_context = dynamic_cast<const SpanContext *>(reference.second)) {
      parent_span_context = span_context;
      break;
    }
  }
  if (parent_span_context != nullptr) {
    trace_id = parent_span_context->trace_id();
    parent_id = parent_span_context->id();
    context_ = parent_span_context->withId(span_id);
  }
}

Span::Span(Span &&other)
    : tracer_(other.tracer_),
      get_time_(other.get_time_),
      writer_(other.writer_),
      start_time_(other.start_time_),
      name(other.name),
      service(other.service),
      resource(other.resource),
      type(other.type),
      span_id(other.span_id),
      trace_id(other.trace_id),
      parent_id(other.parent_id),
      error(other.error),
      start(other.start),
      duration(other.duration),
      meta(other.meta),
      context_(std::move(other.context_)) {
  is_finished_ = (bool)other.is_finished_;  // Copy the value.
}

Span::~Span() {
  if (!is_finished_) {
    this->Finish();
  }
}

void Span::FinishWithOptions(const ot::FinishSpanOptions &finish_span_options) noexcept try {
  if (is_finished_.exchange(true)) {
    return;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  // Set end time.
  auto end_time = get_time_();
  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
  // Apply any special datadog tags. Apply them now rather than when the tag was set, so we don't
  // get a race between SetOperationName and setting span.name or span.resource.
  if (meta.find(datadog_span_type_tag) != meta.end()) {
    type = meta[datadog_span_type_tag];
  }
  if (meta.find(datadog_resource_name_tag) != meta.end()) {
    resource = meta[datadog_resource_name_tag];
  }
  if (meta.find(datadog_service_name_tag) != meta.end()) {
    service = meta[datadog_service_name_tag];
  } else if (meta.find(opentracing_component_tag) != meta.end()) {
    service = meta[opentracing_component_tag];
  }
  meta[opentracing_component_tag] = service;
  writer_->write(std::move(*this));
} catch (const std::bad_alloc &) {
  // At least don't crash.
}

void Span::SetOperationName(ot::string_view name_) noexcept {
  std::lock_guard<std::mutex> lock_guard{mutex_};
  name = name_;
  resource = name_;
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
    meta[key] = result;
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
  return trace_id;  // Never modified, hence un-locked access.
}

}  // namespace opentracing
}  // namespace datadog
