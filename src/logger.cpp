#include "logger.h"
#include "bool.h"

#include <cstdlib>

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

bool isDebug() {
  auto debug = std::getenv("DD_TRACE_DEBUG");
  // Defaults to false unless env var is set to "true"-looking value.
  return debug != nullptr && stob(debug, false);
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

std::shared_ptr<const Logger> makeLogger(const TracerOptions& options) {
  if (isDebug()) {
    return std::make_shared<VerboseLogger>(options.log_func);
  }
  return std::make_shared<StandardLogger>(options.log_func);
}

}  // namespace opentracing
}  // namespace datadog
