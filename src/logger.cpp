#include "logger.h"

namespace datadog {
namespace opentracing {

namespace {

std::string format_message(uint64_t trace_id, ot::string_view message) {
  return std::string("[trace_id: ") + std::to_string(trace_id) + std::string("] ") +
         std::string(message);
}

std::string format_message(uint64_t trace_id, uint64_t span_id, ot::string_view message) {
  return std::string("[trace_id: ") + std::to_string(trace_id) + std::string(", span_id: ") +
         std::to_string(span_id) + std::string("] ") + std::string(message);
}

}  // namespace

void StandardLogger::Log(LogLevel level, ot::string_view message) const noexcept {
  log_func_(level, ot::string_view{message});
}

void StandardLogger::Log(LogLevel level, uint64_t trace_id, ot::string_view message) const
    noexcept {
  log_func_(level, ot::string_view{format_message(trace_id, message)});
}

void StandardLogger::Log(LogLevel level, uint64_t trace_id, uint64_t span_id,
                         ot::string_view message) const noexcept {
  log_func_(level, format_message(trace_id, span_id, message));
}

void VerboseLogger::Log(LogLevel level, ot::string_view message) const noexcept {
  log_func_(level, message);
}

void VerboseLogger::Log(LogLevel level, uint64_t trace_id, ot::string_view message) const
    noexcept {
  log_func_(level, format_message(trace_id, message));
}

void VerboseLogger::Log(LogLevel level, uint64_t trace_id, uint64_t span_id,
                        ot::string_view message) const noexcept {
  log_func_(level, format_message(trace_id, span_id, message));
}

void VerboseLogger::Trace(ot::string_view message) const noexcept {
  log_func_(LogLevel::debug, message);
}

void VerboseLogger::Trace(uint64_t trace_id, ot::string_view message) const noexcept {
  log_func_(LogLevel::debug, format_message(trace_id, message));
}

void VerboseLogger::Trace(uint64_t trace_id, uint64_t span_id, ot::string_view message) const
    noexcept {
  log_func_(LogLevel::debug, format_message(trace_id, span_id, message));
}

}  // namespace opentracing
}  // namespace datadog
